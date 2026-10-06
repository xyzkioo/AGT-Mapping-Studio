// Offline OctoMap preview from optimized body-frame PCD patches.
// Build: g++ -O2 -std=c++17 build_octomap.cpp -o build_octomap $(pkg-config --cflags --libs octomap)
#include <octomap/OcTree.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
bool filter_self = true;
struct Pose {
  std::string patch;
  std::array<double, 3> t;
  std::array<double, 4> q; // w, x, y, z
};

std::array<double, 3> rotate(const std::array<double, 3>& p,
                             const std::array<double, 4>& q) {
  const double w = q[0], x = q[1], y = q[2], z = q[3];
  const double tx = 2.0 * (y * p[2] - z * p[1]);
  const double ty = 2.0 * (z * p[0] - x * p[2]);
  const double tz = 2.0 * (x * p[1] - y * p[0]);
  return {p[0] + w * tx + y * tz - z * ty,
          p[1] + w * ty + z * tx - x * tz,
          p[2] + w * tz + x * ty - y * tx};
}

template <typename T>
std::vector<T> read_raw(const std::string& path, size_t count) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("Cannot read " + path);
  std::vector<T> a(count);
  in.read(reinterpret_cast<char*>(a.data()), count * sizeof(T));
  if (in.gcount() != static_cast<std::streamsize>(count * sizeof(T)))
    throw std::runtime_error("Wrong raw array size: " + path);
  return a;
}

void write_raw(const std::string& path, const std::vector<uint8_t>& a) {
  std::ofstream out(path, std::ios::binary);
  if (!out) throw std::runtime_error("Cannot write " + path);
  out.write(reinterpret_cast<const char*>(a.data()), a.size());
}

std::vector<Pose> read_poses(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("Cannot read poses");
  std::vector<Pose> poses;
  std::string stamp;
  Pose p;
  while (in >> p.patch >> stamp >> p.t[0] >> p.t[1] >> p.t[2]
            >> p.q[0] >> p.q[1] >> p.q[2] >> p.q[3]) {
    poses.push_back(p);
  }
  if (poses.empty()) throw std::runtime_error("No poses");
  return poses;
}

struct ScanCounts {size_t accepted = 0, self = 0, invalid = 0;};

uint64_t packed_key(const octomap::OcTreeKey& key) {
  return (static_cast<uint64_t>(key[0]) << 32) |
         (static_cast<uint64_t>(key[1]) << 16) | key[2];
}

ScanCounts add_scan(octomap::OcTree& tree, const Pose& pose,
                    const std::string& patchdir,
                    std::unordered_map<uint64_t, uint16_t>& free_frame_support) {
  std::ifstream in(patchdir + "/" + pose.patch, std::ios::binary);
  if (!in) throw std::runtime_error("Cannot read " + pose.patch);
  std::string line;
  size_t declared_points = 0;
  bool binary = false;
  while (std::getline(in, line)) {
    if (line.rfind("POINTS ", 0) == 0) declared_points = std::stoull(line.substr(7));
    if (line == "DATA binary") {binary = true; break;}
  }
  if (!binary || !declared_points) throw std::runtime_error("Unsupported PCD " + pose.patch);
  const auto origin_offset = rotate({-0.011, -0.02329, 0.04412}, pose.q);
  const octomap::point3d origin(pose.t[0] + origin_offset[0],
                                pose.t[1] + origin_offset[1],
                                pose.t[2] + origin_offset[2]);
  octomap::Pointcloud scan;
  ScanCounts counts;
  for (size_t i = 0; i < declared_points; ++i) {
    float xyz_i[4];
    in.read(reinterpret_cast<char*>(xyz_i), sizeof(xyz_i));
    if (!in) throw std::runtime_error("Truncated PCD " + pose.patch);
    const std::array<double, 3> body{xyz_i[0], xyz_i[1], xyz_i[2]};
    if (!std::isfinite(body[0]) || !std::isfinite(body[1]) || !std::isfinite(body[2])) {
      ++counts.invalid; continue;
    }
    // Use the BODY/IMU box used by the offline self-return algorithm.
    const double lx = body[0], ly = body[1];
    const double lz = body[2];
    if (filter_self && lx >= -0.85 && lx <= -0.4 && ly >= -0.2 && ly <= 0.2 &&
        lz >= -0.2 && lz <= 1.0) {
      ++counts.self; continue;
    }
    const auto world = rotate(body, pose.q);
    const octomap::point3d endpoint(pose.t[0] + world[0],
                                     pose.t[1] + world[1],
                                     pose.t[2] + world[2]);
    if ((endpoint - origin).norm() < 0.5f) {++counts.invalid; continue;}
    scan.push_back(endpoint);
    ++counts.accepted;
  }
  // Same discrete update as insertPointCloud(..., discretize=true), while
  // recording each free voxel only once per distinct keyframe.
  octomap::KeySet free_cells, occupied_cells;
  tree.computeDiscreteUpdate(scan, origin, free_cells, occupied_cells, 25.0);
  for (const auto& key : free_cells) {
    tree.updateNode(key, false, false);
    auto& count = free_frame_support[packed_key(key)];
    if (count < std::numeric_limits<uint16_t>::max()) ++count;
  }
  for (const auto& key : occupied_cells) tree.updateNode(key, true, false);
  return counts;
}
} // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 14) {
      std::cerr << "usage: build_octomap poses patches ground.f32 valid.u8 before.u8 "
                   "width height origin_x origin_y grid_resolution output_dir octree_resolution self_filter\n";
      return 2;
    }
    const std::string poses_path = argv[1], patchdir = argv[2];
    filter_self = std::stoi(argv[13]) != 0;
    const int width = std::stoi(argv[6]), height = std::stoi(argv[7]);
    const double x0 = std::stod(argv[8]), y0 = std::stod(argv[9]);
    const double gridres = std::stod(argv[10]), resolution = std::stod(argv[12]);
    const std::string outdir = argv[11];
    const size_t n = static_cast<size_t>(width) * height;
    const auto ground = read_raw<float>(argv[3], n);
    const auto valid = read_raw<uint8_t>(argv[4], n);
    const auto before = read_raw<uint8_t>(argv[5], n);
    const auto poses = read_poses(poses_path);
    octomap::OcTree tree(resolution);
    std::unordered_map<uint64_t, uint16_t> free_frame_support;
    size_t accepted = 0, self = 0, invalid = 0;
    for (size_t i = 0; i < poses.size(); ++i) {
      const auto c = add_scan(tree, poses[i], patchdir, free_frame_support);
      accepted += c.accepted; self += c.self; invalid += c.invalid;
      if ((i + 1) % 30 == 0 || i + 1 == poses.size()) {
        std::cout << "inserted " << i + 1 << "/" << poses.size()
                  << " scans; leaves=" << tree.getNumLeafNodes() << std::endl;
      }
    }
    tree.updateInnerOccupancy();
    if (!tree.writeBinary(outdir + "/keyframes.bt"))
      throw std::runtime_error("Failed to write OctoMap");
    std::vector<uint8_t> free(n, 0), occupied(n, 0), known(n, 0);
    std::vector<uint8_t> minimum_frame_support(n, 0);
    size_t valid_columns = 0;
    for (int iy = 0; iy < height; ++iy) {
      for (int ix = 0; ix < width; ++ix) {
        const size_t idx = static_cast<size_t>(iy) * width + ix;
        if (!valid[idx] || !std::isfinite(ground[idx]) || before[idx] == 0) continue;
        ++valid_columns;
        const double x = x0 + (ix + 0.5) * gridres;
        const double y = y0 + (iy + 0.5) * gridres;
        uint8_t min_frames = 255;
        // Eight samples of the 0.2--1.8 m vehicle-clearance band.
        for (int k = 0; k < 8; ++k) {
          const double z = ground[idx] + 0.3 + k * 0.2;
          auto* node = tree.search(x, y, z);
          if (!node) {min_frames = 0; continue;}
          ++known[idx];
          if (tree.isNodeOccupied(node)) {++occupied[idx]; min_frames = 0;}
          else if (node->getOccupancy() <= 0.45) {
            ++free[idx];
            const octomap::OcTreeKey key = tree.coordToKey(x, y, z);
            const auto found = free_frame_support.find(packed_key(key));
            const uint8_t frames = found == free_frame_support.end() ? 0 :
                static_cast<uint8_t>(std::min<uint16_t>(found->second, 255));
            min_frames = std::min(min_frames, frames);
          } else min_frames = 0;
        }
        minimum_frame_support[idx] = min_frames;
      }
    }
    write_raw(outdir + "/clearance_free.u8", free);
    write_raw(outdir + "/clearance_occupied.u8", occupied);
    write_raw(outdir + "/clearance_known.u8", known);
    write_raw(outdir + "/clearance_min_frame_support.u8", minimum_frame_support);
    std::ofstream report(outdir + "/octomap_build.txt");
    report << "keyframes=" << poses.size() << "\naccepted_points=" << accepted
           << "\nexcluded_self_returns=" << self << "\ninvalid_or_near_points=" << invalid
           << "\nleaf_nodes=" << tree.getNumLeafNodes()
           << "\nfree_voxels_with_frame_support=" << free_frame_support.size()
           << "\nvalid_columns=" << valid_columns << "\nresolution_m=" << resolution
           << "\nmax_ray_m=25\n";
    std::cout << "projected " << valid_columns << " columns; accepted=" << accepted
              << " self=" << self << " invalid=" << invalid << std::endl;
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}

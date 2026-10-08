#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "livox_ros_driver2/msg/custom_msg.hpp"
#include "rclcpp/rclcpp.hpp"

namespace agt_livox_self_return_filter
{

class LivoxSelfReturnFilterNode : public rclcpp::Node
{
public:
  using CustomMsg = livox_ros_driver2::msg::CustomMsg;

  LivoxSelfReturnFilterNode()
  : Node("livox_self_return_filter_node")
  {
    input_topic_ = declare_parameter<std::string>(
      "input_topic", "/agt/sensors/lidar/custom");
    output_topic_ = declare_parameter<std::string>(
      "output_topic", "/mapping/sensor/livox_prefiltered");
    enabled_ = declare_parameter<bool>("enabled", true);
    report_every_scans_ = declare_parameter<int>("report_every_scans", 100);

    const auto box_min = declare_parameter<std::vector<double>>(
      "box_min", {-0.82, -0.18, -0.10});
    const auto box_max = declare_parameter<std::vector<double>>(
      "box_max", {-0.48, 0.18, 0.70});
    if (box_min.size() != 3 || box_max.size() != 3) {
      throw std::runtime_error("box_min and box_max must each contain exactly three numbers");
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
      if (!std::isfinite(box_min[axis]) || !std::isfinite(box_max[axis]) ||
        box_min[axis] >= box_max[axis])
      {
        throw std::runtime_error("invalid self-return box bounds");
      }
      box_min_[axis] = box_min[axis];
      box_max_[axis] = box_max[axis];
    }
    if (report_every_scans_ < 0) {
      throw std::runtime_error("report_every_scans must be >= 0");
    }

    publisher_ = create_publisher<CustomMsg>(
      output_topic_, rclcpp::QoS(rclcpp::KeepLast(20)).reliable());
    subscription_ = create_subscription<CustomMsg>(
      input_topic_, rclcpp::SensorDataQoS(),
      std::bind(&LivoxSelfReturnFilterNode::filter_callback, this, std::placeholders::_1));

    RCLCPP_INFO(
      get_logger(),
      "Livox self-return filter: %s -> %s, enabled=%s, "
      "box=[%.3f %.3f %.3f]..[%.3f %.3f %.3f] in incoming lidar coordinates",
      input_topic_.c_str(), output_topic_.c_str(), enabled_ ? "true" : "false",
      box_min_[0], box_min_[1], box_min_[2],
      box_max_[0], box_max_[1], box_max_[2]);
  }

private:
  bool inside_box(const livox_ros_driver2::msg::CustomPoint & point) const
  {
    return static_cast<double>(point.x) >= box_min_[0] &&
           static_cast<double>(point.x) <= box_max_[0] &&
           static_cast<double>(point.y) >= box_min_[1] &&
           static_cast<double>(point.y) <= box_max_[1] &&
           static_cast<double>(point.z) >= box_min_[2] &&
           static_cast<double>(point.z) <= box_max_[2];
  }

  void filter_callback(const CustomMsg::ConstSharedPtr message)
  {
    ++scan_count_;
    input_points_ += message->points.size();

    if (!enabled_) {
      output_points_ += message->points.size();
      publisher_->publish(*message);
      report_if_needed();
      return;
    }

    CustomMsg filtered = *message;
    filtered.points.clear();
    filtered.points.reserve(message->points.size());

    std::size_t removed_this_scan = 0;
    for (std::size_t index = 0; index < message->points.size(); ++index) {
      const auto & point = message->points[index];
      if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
        filtered.points.push_back(point);
        continue;
      }
      // Autoware's crop-box self filter uses negative=true to remove inside points.
      // Preserve the original final point only to protect the downstream scan-end
      // convention when filtering Livox CustomMsg before the mapping adapter.
      if (index + 1 != message->points.size() && inside_box(point)) {
        ++removed_this_scan;
        continue;
      }
      filtered.points.push_back(point);
    }

    filtered.point_num = static_cast<std::uint32_t>(filtered.points.size());
    removed_points_ += removed_this_scan;
    output_points_ += filtered.points.size();

    // Copying the original CustomPoint object preserves offset_time,
    // reflectivity, tag and line exactly for every retained point.
    publisher_->publish(filtered);
    report_if_needed();
  }

  void report_if_needed()
  {
    if (report_every_scans_ == 0 || scan_count_ % static_cast<std::uint64_t>(report_every_scans_) != 0) {
      return;
    }
    const double fraction = input_points_ == 0 ? 0.0 :
      100.0 * static_cast<double>(removed_points_) / static_cast<double>(input_points_);
    RCLCPP_INFO(
      get_logger(),
      "scans=%lu input=%lu kept=%lu removed=%lu (%.4f%%)",
      static_cast<unsigned long>(scan_count_),
      static_cast<unsigned long>(input_points_),
      static_cast<unsigned long>(output_points_),
      static_cast<unsigned long>(removed_points_), fraction);
  }

  std::string input_topic_;
  std::string output_topic_;
  bool enabled_{true};
  int report_every_scans_{100};
  std::array<double, 3> box_min_{};
  std::array<double, 3> box_max_{};

  std::uint64_t scan_count_{0};
  std::uint64_t input_points_{0};
  std::uint64_t output_points_{0};
  std::uint64_t removed_points_{0};

  rclcpp::Publisher<CustomMsg>::SharedPtr publisher_;
  rclcpp::Subscription<CustomMsg>::SharedPtr subscription_;
};

}  // namespace agt_livox_self_return_filter

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<agt_livox_self_return_filter::LivoxSelfReturnFilterNode>());
  rclcpp::shutdown();
  return 0;
}

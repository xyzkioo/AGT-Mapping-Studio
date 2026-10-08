#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>

#include "livox_ros_driver2/msg/custom_msg.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "sensor_msgs/point_cloud2_iterator.hpp"

namespace agt_livox_self_filter_bridge
{

class LivoxSelfFilterBridgeNode : public rclcpp::Node
{
public:
  LivoxSelfFilterBridgeNode()
  : Node("livox_self_filter_bridge_node")
  {
    const auto input_livox = declare_parameter<std::string>(
      "input_livox_topic", "/mapping/sensor/livox_unfiltered");
    const auto input_cloud = declare_parameter<std::string>(
      "input_filtered_cloud_topic", "/mapping/sensor/cloud_filtered");
    const auto output_livox = declare_parameter<std::string>(
      "output_livox_topic", "/mapping/sensor/livox");
    const auto output_cloud = declare_parameter<std::string>(
      "output_cloud_topic", "/mapping/sensor/cloud");
    max_cache_size_ = static_cast<std::size_t>(
      std::max<int64_t>(2, declare_parameter<int64_t>("max_cache_size", 40)));

    livox_pub_ = create_publisher<livox_ros_driver2::msg::CustomMsg>(output_livox, rclcpp::QoS(20));
    cloud_pub_ = create_publisher<sensor_msgs::msg::PointCloud2>(output_cloud, rclcpp::SensorDataQoS());

    livox_sub_ = create_subscription<livox_ros_driver2::msg::CustomMsg>(
      input_livox, rclcpp::QoS(20),
      [this](livox_ros_driver2::msg::CustomMsg::ConstSharedPtr message) {
        cache_.push_back(message);
        while (cache_.size() > max_cache_size_) {
          cache_.pop_front();
        }
      });

    cloud_sub_ = create_subscription<sensor_msgs::msg::PointCloud2>(
      input_cloud, rclcpp::SensorDataQoS(),
      std::bind(&LivoxSelfFilterBridgeNode::filteredCloudCallback, this, std::placeholders::_1));

    RCLCPP_INFO(
      get_logger(), "Self-filter bridge: %s + %s -> %s",
      input_livox.c_str(), input_cloud.c_str(), output_livox.c_str());
  }

private:
  using LivoxMsg = livox_ros_driver2::msg::CustomMsg;

  static bool sameStamp(
    const builtin_interfaces::msg::Time & a,
    const builtin_interfaces::msg::Time & b)
  {
    return a.sec == b.sec && a.nanosec == b.nanosec;
  }

  void filteredCloudCallback(const sensor_msgs::msg::PointCloud2::ConstSharedPtr cloud)
  {
    const auto match = std::find_if(
      cache_.begin(), cache_.end(),
      [&cloud](const LivoxMsg::ConstSharedPtr & item) {
        return sameStamp(item->header.stamp, cloud->header.stamp);
      });
    if (match == cache_.end()) {
      ++unmatched_clouds_;
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000,
        "No matching Livox packet for filtered cloud (unmatched=%zu)", unmatched_clouds_);
      return;
    }

    const auto source = *match;
    const std::size_t cloud_points =
      static_cast<std::size_t>(cloud->width) * static_cast<std::size_t>(cloud->height);
    if (cloud_points != source->points.size()) {
      RCLCPP_ERROR_THROTTLE(
        get_logger(), *get_clock(), 5000,
        "robot_self_filter must use keep_organized=true: cloud=%zu Livox=%zu",
        cloud_points, source->points.size());
      cache_.erase(match);
      return;
    }

    LivoxMsg filtered = *source;
    filtered.points.clear();
    filtered.points.reserve(source->points.size());

    sensor_msgs::PointCloud2ConstIterator<float> x(*cloud, "x");
    sensor_msgs::PointCloud2ConstIterator<float> y(*cloud, "y");
    sensor_msgs::PointCloud2ConstIterator<float> z(*cloud, "z");
    for (std::size_t i = 0; i < source->points.size(); ++i, ++x, ++y, ++z) {
      if (std::isfinite(*x) && std::isfinite(*y) && std::isfinite(*z)) {
        filtered.points.push_back(source->points[i]);
      }
    }
    filtered.point_num = static_cast<std::uint32_t>(filtered.points.size());

    livox_pub_->publish(filtered);
    cloud_pub_->publish(*cloud);
    cache_.erase(match);

    RCLCPP_DEBUG_THROTTLE(
      get_logger(), *get_clock(), 5000,
      "Self filter kept %u/%zu Livox points", filtered.point_num, source->points.size());
  }

  std::size_t max_cache_size_{40};
  std::size_t unmatched_clouds_{0};
  std::deque<LivoxMsg::ConstSharedPtr> cache_;
  rclcpp::Subscription<LivoxMsg>::SharedPtr livox_sub_;
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr cloud_sub_;
  rclcpp::Publisher<LivoxMsg>::SharedPtr livox_pub_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr cloud_pub_;
};

}  // namespace agt_livox_self_filter_bridge

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<agt_livox_self_filter_bridge::LivoxSelfFilterBridgeNode>());
  rclcpp::shutdown();
  return 0;
}

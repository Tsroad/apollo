/******************************************************************************
 * Copyright 2019 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "modules/prediction/common/semantic_map.h"

#include <cmath>
#include <string>
#include <vector>

#include "cyber/common/file.h"
#include "cyber/common/log.h"
#include "modules/common/configs/config_gflags.h"
#include "modules/common/util/string_util.h"
#include "modules/common/util/util.h"
#include "modules/map/hdmap/hdmap_util.h"
#include "modules/prediction/common/prediction_gflags.h"
#include "modules/prediction/container/container_manager.h"
#include "modules/prediction/container/pose/pose_container.h"

namespace apollo {
namespace prediction {

SemanticMap::SemanticMap() {}

void SemanticMap::Init() {
  const std::string semantic_map_path =
      apollo::common::util::StrCat(FLAGS_map_dir, "/semantic_map.png");
  if (cyber::common::PathExists(semantic_map_path)) {
    base_img_ = cv::imread(semantic_map_path, CV_LOAD_IMAGE_COLOR);
    AINFO << "Load semantic_map from: " << semantic_map_path;
  }
  const std::string config_path = apollo::common::util::StrCat(
      FLAGS_map_dir, "/semantic_map_config.pb.txt");
  if (!cyber::common::GetProtoFromFile(config_path, &config_)) {
    AERROR << "Failed to load config file: " << config_path;
    return;
  }
  curr_img_ = cv::Mat(2000, 2000, CV_8UC3, cv::Scalar(0, 0, 0));
}

void SemanticMap::RunCurrFrame(
    const std::unordered_map<int, ObstacleHistory>& obstacle_id_history_map) {
  if (obstacle_id_history_map.find(FLAGS_ego_vehicle_id) ==
      obstacle_id_history_map.end()) {
    return;
  }
  obstacle_id_history_map_ = obstacle_id_history_map;
  ego_feature_ = obstacle_id_history_map_.at(FLAGS_ego_vehicle_id).feature(0);
  // curr_base_x_ = ego_feature.position().x() - config_.observation_range();
  // curr_base_y_ = ego_feature.position().y() - config_.observation_range();
  // cv::Rect rect(static_cast<int>((curr_base_x_ - config_.base_point().x()) /
  //                                config_.resolution()),
  //               static_cast<int>(config_.dim_y() -
  //                                (curr_base_y_ - config_.base_point().y()) /
  //                                    config_.resolution()) -
  //                   2000,
  //               2000, 2000);
  // base_img_(rect).copyTo(curr_img_);
  // TODO(all): move to somewhere else
  DrawBaseMap();
  base_img_.copyTo(curr_img_);

  // Draw all obstacles_history
  for (const auto obstacle_id_history_pair : obstacle_id_history_map_) {
    DrawHistory(obstacle_id_history_pair.second, cv::Scalar(0, 255, 255),
                &curr_img_);
  }

  // Crop ego_vehicle for demo
  if (false) {
    cv::Mat output_img;
    if (GetMapById(FLAGS_ego_vehicle_id, &output_img)) {
      cv::namedWindow("Demo window", cv::WINDOW_NORMAL);
      cv::imshow("Demo window", output_img);
      cv::waitKey();
    }
  }
}

void SemanticMap::DrawBaseMap() {
  double x = ego_feature_.position().x();
  double y = ego_feature_.position().y();
  base_img_ = cv::Mat(2000, 2000, CV_8UC3, cv::Scalar(0, 0, 0));
  curr_base_x_ = x - 100.0;
  curr_base_y_ = y - 100.0;
  common::PointENU center_point = common::util::MakePointENU(x, y, 0.0);
  DrawRoads(center_point);
  DrawJunctions(center_point);
  DrawCrosswalks(center_point);
  DrawLanes(center_point);
}

void SemanticMap::DrawRoads(const common::PointENU& center_point,
                            const cv::Scalar& color) {
  std::vector<apollo::hdmap::RoadInfoConstPtr> roads;
  apollo::hdmap::HDMapUtil::BaseMap().GetRoads(center_point, 141.4, &roads);
  for (const auto& road : roads) {
    for (const auto& section : road->road().section()) {
      std::vector<cv::Point> polygon;
      for (const auto& edge : section.boundary().outer_polygon().edge()) {
        if (edge.type() == 2) {  // left edge
          for (const auto& segment : edge.curve().segment()) {
            for (const auto& point : segment.line_segment().point()) {
              polygon.push_back(GetTransPoint(point.x(), point.y()));
            }
          }
        } else if (edge.type() == 3) {  // right edge
          for (const auto& segment : edge.curve().segment()) {
            for (const auto& point : segment.line_segment().point()) {
              polygon.insert(polygon.begin(),
                             GetTransPoint(point.x(), point.y()));
            }
          }
        }
      }
      cv::fillPoly(base_img_, std::vector<std::vector<cv::Point>>({polygon}),
                   color);
    }
  }
}

void SemanticMap::DrawJunctions(const common::PointENU& center_point,
                                const cv::Scalar& color) {
  std::vector<apollo::hdmap::JunctionInfoConstPtr> junctions;
  apollo::hdmap::HDMapUtil::BaseMap().GetJunctions(center_point, 141.4,
                                                   &junctions);
  for (const auto& junction : junctions) {
    std::vector<cv::Point> polygon;
    for (const auto& point : junction->junction().polygon().point()) {
      polygon.push_back(GetTransPoint(point.x(), point.y()));
    }
    cv::fillPoly(base_img_, std::vector<std::vector<cv::Point>>({polygon}),
                 color);
  }
}

void SemanticMap::DrawCrosswalks(const common::PointENU& center_point,
                                 const cv::Scalar& color) {
  std::vector<apollo::hdmap::CrosswalkInfoConstPtr> crosswalks;
  apollo::hdmap::HDMapUtil::BaseMap().GetCrosswalks(center_point, 141.4,
                                                    &crosswalks);
  for (const auto& crosswalk : crosswalks) {
    std::vector<cv::Point> polygon;
    for (const auto& point : crosswalk->crosswalk().polygon().point()) {
      polygon.push_back(GetTransPoint(point.x(), point.y()));
    }
    cv::fillPoly(base_img_, std::vector<std::vector<cv::Point>>({polygon}),
                 color);
  }
}

void SemanticMap::DrawLanes(const common::PointENU& center_point,
                            const cv::Scalar& color) {
  std::vector<apollo::hdmap::LaneInfoConstPtr> lanes;
  apollo::hdmap::HDMapUtil::BaseMap().GetLanes(center_point, 141.4, &lanes);
  for (const auto& lane : lanes) {
    // Draw lane_central first
    for (const auto& segment : lane->lane().central_curve().segment()) {
      for (int i = 0; i < segment.line_segment().point_size() - 1; ++i) {
        const auto& p0 = GetTransPoint(segment.line_segment().point(i).x(),
                                       segment.line_segment().point(i).y());
        const auto& p1 = GetTransPoint(segment.line_segment().point(i + 1).x(),
                                       segment.line_segment().point(i + 1).y());
        double theta = atan2(segment.line_segment().point(i + 1).y() -
                                 segment.line_segment().point(i).y(),
                             segment.line_segment().point(i + 1).x() -
                                 segment.line_segment().point(i).x());
        double H = theta >= 0 ? theta / (2 * M_PI) : theta / (2 * M_PI) + 1;
        // // Original cv::cvtColor() is 4 times slower than HSVtoRGB()
        // cv::Mat hsv(1, 1, CV_32FC3, cv::Scalar(H * 360.0, 1.0, 1.0));
        // cv::Mat rgb;
        // cv::cvtColor(hsv, rgb, cv::COLOR_HSV2RGB);
        // cv::Scalar c =
        //     cv::Scalar(rgb.at<float>(0, 0) * 255, rgb.at<float>(0, 1) * 255,
        //                rgb.at<float>(0, 2) * 255);

        cv::line(base_img_, p0, p1, HSVtoRGB(H), 4);
      }
    }
    // Not drawing boundary for virtual city_driving lane
    if (lane->lane().type() == 2 && lane->lane().left_boundary().virtual_() &&
        lane->lane().right_boundary().virtual_()) {
      continue;
    }
    // Draw lane's left_boundary
    for (const auto& segment : lane->lane().left_boundary().curve().segment()) {
      for (int i = 0; i < segment.line_segment().point_size() - 1; ++i) {
        const auto& p0 = GetTransPoint(segment.line_segment().point(i).x(),
                                       segment.line_segment().point(i).y());
        const auto& p1 = GetTransPoint(segment.line_segment().point(i + 1).x(),
                                       segment.line_segment().point(i + 1).y());
        cv::line(base_img_, p0, p1, color, 2);
      }
    }
    // Draw lane's right_boundary
    for (const auto& segment :
         lane->lane().right_boundary().curve().segment()) {
      for (int i = 0; i < segment.line_segment().point_size() - 1; ++i) {
        const auto& p0 = GetTransPoint(segment.line_segment().point(i).x(),
                                       segment.line_segment().point(i).y());
        const auto& p1 = GetTransPoint(segment.line_segment().point(i + 1).x(),
                                       segment.line_segment().point(i + 1).y());
        cv::line(base_img_, p0, p1, color, 2);
      }
    }
  }
}

cv::Scalar SemanticMap::HSVtoRGB(double H, double S, double V) {
  int I = static_cast<int>(floor(H * 6.0));
  double F = H * 6.0 - floor(H * 6.0);
  double M = V * (1.0 - S);
  double N = V * (1.0 - S * F);
  double K = V * (1.0 - S * (1.0 - F));
  switch (I) {
    case 0:
      return cv::Scalar(V, K, M) * 255.0;
    case 1:
      return cv::Scalar(N, V, M) * 255.0;
    case 2:
      return cv::Scalar(M, V, K) * 255.0;
    case 3:
      return cv::Scalar(M, N, V) * 255.0;
    case 4:
      return cv::Scalar(K, M, V) * 255.0;
    default:
      return cv::Scalar(V, M, N) * 255.0;
  }
}

void SemanticMap::DrawRect(const Feature& feature, const cv::Scalar& color,
                           cv::Mat* img) {
  double obs_l = feature.length();
  double obs_w = feature.width();
  double obs_x = feature.position().x();
  double obs_y = feature.position().y();
  double theta = feature.theta();
  std::vector<cv::Point> polygon;
  // point 1 (head-right point)
  polygon.push_back(
      GetTransPoint(obs_x + (cos(theta) * obs_l - sin(theta) * obs_w) / 2,
                    obs_y + (sin(theta) * obs_l + cos(theta) * obs_w) / 2));
  // point 2 (head-left point)
  polygon.push_back(
      GetTransPoint(obs_x + (cos(theta) * -obs_l - sin(theta) * obs_w) / 2,
                    obs_y + (sin(theta) * -obs_l + cos(theta) * obs_w) / 2));
  // point 3 (back-left point)
  polygon.push_back(
      GetTransPoint(obs_x + (cos(theta) * -obs_l - sin(theta) * -obs_w) / 2,
                    obs_y + (sin(theta) * -obs_l + cos(theta) * -obs_w) / 2));
  // point 4 (back-right point)
  polygon.push_back(
      GetTransPoint(obs_x + (cos(theta) * obs_l - sin(theta) * -obs_w) / 2,
                    obs_y + (sin(theta) * obs_l + cos(theta) * -obs_w) / 2));
  cv::fillPoly(*img, std::vector<std::vector<cv::Point>>({polygon}), color);
}

void SemanticMap::DrawPoly(const Feature& feature, const cv::Scalar& color,
                           cv::Mat* img) {
  std::vector<cv::Point> polygon;
  for (auto& polygon_point : feature.polygon_point()) {
    polygon.push_back(GetTransPoint(polygon_point.x(), polygon_point.y()));
  }
  cv::fillPoly(*img, std::vector<std::vector<cv::Point>>({polygon}), color);
}

void SemanticMap::DrawHistory(const ObstacleHistory& history,
                              const cv::Scalar& color, cv::Mat* img) {
  for (int i = history.feature_size() - 1; i >= 0; --i) {
    const Feature& feature = history.feature(i);
    double time_decay = 1.0 - ego_feature_.timestamp() + feature.timestamp();
    cv::Scalar decay_color = color * time_decay;
    if (feature.id() == FLAGS_ego_vehicle_id) {
      DrawRect(feature, decay_color, img);
    } else {
      DrawPoly(feature, decay_color, img);
    }
  }
}

cv::Mat SemanticMap::CropArea(const cv::Mat& input_img,
                              const cv::Point2i& center_point,
                              const double heading) {
  cv::Mat rotation_mat =
      cv::getRotationMatrix2D(center_point, 90.0 - heading * 180.0 / M_PI, 1.0);
  cv::Mat rotated_mat;
  cv::warpAffine(input_img, rotated_mat, rotation_mat, input_img.size());
  cv::Rect rect(center_point.x - 200, center_point.y - 300, 400, 400);
  cv::Mat output_img;
  cv::resize(rotated_mat(rect), output_img, cv::Size(224, 224));
  return output_img;
}

cv::Mat SemanticMap::CropByHistory(const ObstacleHistory& history,
                                   const cv::Scalar& color) {
  cv::Mat feature_map = curr_img_.clone();
  DrawHistory(history, color, &feature_map);
  const Feature& curr_feature = history.feature(0);
  cv::Point2i center_point =
      GetTransPoint(curr_feature.position().x(), curr_feature.position().y());
  return CropArea(feature_map, center_point, curr_feature.theta());
}

bool SemanticMap::GetMapById(const int obstacle_id, cv::Mat* feature_map) {
  if (obstacle_id_history_map_.find(obstacle_id) ==
      obstacle_id_history_map_.end()) {
    return false;
  }
  cv::Mat output_img = CropByHistory(obstacle_id_history_map_[obstacle_id],
                                     cv::Scalar(0, 0, 255));
  output_img.copyTo(*feature_map);
  return true;
}

}  // namespace prediction
}  // namespace apollo

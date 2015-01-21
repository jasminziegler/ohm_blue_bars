
#include "PathPlan_AStar.h"

namespace{
const uint8_t WALL_VALUE = 255;
const uint8_t FREE_VALUE = 0;
}

PathPlan_AStar::PathPlan_AStar() :
      _rate(0)
{
   _loopRate = 0;

   //rosParam
   ros::NodeHandle privNh("~");
   std::string sub_map;
   std::string sub_target;
   std::string sub_pose;
   std::string pub_path;
   std::string frame_id;
   std::string tf_map_frame;
   std::string tf_robot_frame;

   double robot_radius;
   double cost_short_step;
   double cost_long_step ;
   double factor_dist    ;
   //int int_val;

   privNh.param("sub_map",               sub_map,        std::string("map"));
   privNh.param("sub_target",            sub_target,     std::string("/move_base_simple/goal"));
   privNh.param("sub_pose",              sub_pose,       std::string("robot0/pose"));
   privNh.param("pub_path",              pub_path,       std::string("path"));
   privNh.param("frame_id",              frame_id,       std::string("map"));
   privNh.param("tf_map_frame",           tf_map_frame,           std::string("map"));
   privNh.param("tf_robot_frame",         tf_robot_frame,         std::string("base_footprint"));

   privNh.param<double>("robot_radius",     robot_radius   ,   0.35);
   privNh.param<double>("cost_short_step",  cost_short_step,   1);
   privNh.param<double>("cost_long_step",   cost_long_step ,   ::sqrt(2) * 1);
   privNh.param<double>("factor_dist",      factor_dist    ,   1);




   //privNh.param<int>("int_val",int_val, 1234);

   _robot_radius = robot_radius;
   _cost_short_step = cost_short_step;
   _cost_long_step  = cost_long_step ;
   _factor_dist     = factor_dist    ;

   _frame_id = frame_id;
   _tf_map_frame = tf_map_frame;
   _tf_robot_frame = tf_robot_frame;

   //init publisher
   _pubPath = _nh.advertise<nav_msgs::Path>(pub_path,1);

   //inti subscriber
   _subMap        = _nh.subscribe(sub_map, 1, &PathPlan_AStar::subCallback_map, this);
   _subTargetPose = _nh.subscribe(sub_target, 1, &PathPlan_AStar::subCallback_target, this);

   _gotMap = false;
}

PathPlan_AStar::~PathPlan_AStar()
{
   delete _rate;
}

void PathPlan_AStar::start(const unsigned int rate)
{
   delete _rate;
   _loopRate = rate;
   _rate = new ros::Rate(_loopRate);
   this->run();
}

void PathPlan_AStar::run()
{
   ros::spin();
}


void PathPlan_AStar::subCallback_map(const nav_msgs::OccupancyGrid& msg)
{
   std::cout << "got map: " << msg.info.width << " x " << msg.info.height
         << std::endl;
   _map = msg;

   _gotMap = true;
}



void PathPlan_AStar::subCallback_target(const geometry_msgs::PoseStamped& msg)
{
   if(!_gotMap)
   {
      ROS_WARN("ohm_path_plan -> Got no valid Pose or Map until now... doing no path planning");
      return;
   }
   //do pathplan
   ROS_INFO("ohm_path_plan -> Do Pathplan");

   //get tf (current pos)
   tf::StampedTransform tf;
   try {
      ros::Time time = ros::Time::now();
      _tf_listnener.waitForTransform(_tf_map_frame, _tf_robot_frame, time, ros::Duration(5));
      _tf_listnener.lookupTransform(_tf_map_frame, _tf_robot_frame, time, tf);

   } catch (tf::TransformException& e)
   {
      ROS_ERROR("ohm_path_plan -> Exeption at tf: %s", e.what());
      return;
   }

   apps::Point2D pose;

   pose.x = tf.getOrigin().x();
   pose.y = tf.getOrigin().y();

   //obvious::Timer timer;
   //timer.reset();

   apps::Point2D origin;
   origin.x = _map.info.origin.position.x;
   origin.y = _map.info.origin.position.y;

   apps::Point2D end;
   end.x = msg.pose.position.x;
   end.y = msg.pose.position.y;

   ROS_INFO("ohm_path_plan -> Create Planner");
   apps::Astar_dt* astar_planer = new apps::Astar_dt(NULL);
   astar_planer->setWallValue(WALL_VALUE);
   astar_planer->setAstarParam(_cost_short_step,
                               _cost_long_step,
                               _factor_dist);
   astar_planer->setGridMap(new apps::GridMap((uint8_t*) &_map.data[0],
                                               _map.info.width,
                                               _map.info.height,
                                               _map.info.resolution,
                                               origin));


   ROS_INFO("ohm_path_plan -> Do map oerations");
   this->do_map_operations(astar_planer);
   ROS_INFO("ohm_path_plan ->  Do planning");
   std::vector<apps::Point2D> path = this->do_path_planning(astar_planer, pose, end);



   ROS_INFO("ohm_path_plan -> save debug stuff");
   //save map and dt map
   this->debug_save_as_img("/tmp/dt_map.png",astar_planer->getCostmap("dt"),path);
   this->debug_save_as_img("/tmp/map.png",astar_planer->getGridMap(), path);

   ROS_INFO("ohm_path_plan ->  clear mem");
   //clear mem
   delete astar_planer->getGridMap();
   delete astar_planer->getCostmap("dt");
   astar_planer->resetCostmaps();
   delete astar_planer;

   ROS_INFO("ohm_path_plan -> publish data");
   _pubPath.publish(this->toRosPath(path,msg));
}

void PathPlan_AStar::debug_save_as_img(std::string file, apps::GridMap* map,
      std::vector<apps::Point2D> path)
{
   //debug: draw path in cv::Mat
   cv::Mat cvmap = map->toCvMat();
   cv::cvtColor(cvmap, cvmap, CV_GRAY2RGB);

   apps::Pixel p = map->toPixel(path[0]);
   cv::Point old;
   cv::Point curr;
   old.x = p.x;
   old.y = p.y;

   for(unsigned int i = 1; i < path.size(); ++i)
   {
      p = map->toPixel(path[i]);
      curr.x = p.x;
      curr.y = p.y;
      cv::line(cvmap, curr, old, cv::Scalar(0,255,0),1);
      old = curr;
   }

   cv::flip(cvmap, cvmap, -1);
   cv::flip(cvmap, cvmap, 1);
   cv::imwrite(file.c_str(),cvmap);
}

nav_msgs::Path PathPlan_AStar::toRosPath(std::vector<apps::Point2D> path,
      geometry_msgs::PoseStamped target)
{
   nav_msgs::Path msgPath;
   msgPath.header.frame_id = _frame_id;
   for (unsigned int i = 0; i < path.size(); ++i)
   {
      geometry_msgs::PoseStamped tmp;
      tmp.pose.position.x = path[i].x;
      tmp.pose.position.y = path[i].y;
      tmp.pose.position.z = 0;
      tmp.pose.orientation.w = 0;
      tmp.pose.orientation.x = 0;
      tmp.pose.orientation.y = 0;
      tmp.pose.orientation.z = 0;
      tmp.header.frame_id = _frame_id;

      msgPath.poses.push_back(tmp);
   }
   //set orientation of last wp
   msgPath.poses[msgPath.poses.size() - 1].pose.orientation = target.pose.orientation;

   return msgPath;
}

void PathPlan_AStar::do_map_operations(apps::Astar_dt* planner)
{
   //inflate and binarize image
   apps::MapOperations::inflate(planner->getGridMap(), 10, 127, _robot_radius);
   apps::MapOperations::binarize(planner->getGridMap(), 0, 1, FREE_VALUE, WALL_VALUE);

   apps::GridMap* cost_map_dt = new apps::GridMap(planner->getGridMap());
   apps::MapOperations::distnaceTransform(cost_map_dt,0.8,WALL_VALUE);
   planner->addCostmap("dt", cost_map_dt);
}

std::vector<apps::Point2D> PathPlan_AStar::do_path_planning(apps::Astar_dt* planner, apps::Point2D start, apps::Point2D end)
{
   std::vector<apps::Point2D> path = planner->computePathPoint(start, end);

   if(!path.size())
   {
      ROS_INFO("Found no Path");
      return path;
   }
   ROS_INFO("Found Path with length: %d", (int)path.size());
   return path;
}

bool PathPlan_AStar::srvCallback_plan_sorted(
      ohm_path_plan::PlanPathsSortedRequest& req,
      ohm_path_plan::PlanPathsSortedResponse& res)
{

   //todo


   return true;
}

//------------------------------------------------------------------------------------
//-- main --
//----------

int main(int argc, char *argv[])
{
   ros::init(argc, argv, "ohm_path_plan_astar_node");
   ros::NodeHandle nh("~");

   PathPlan_AStar node;
   node.start(10);

}



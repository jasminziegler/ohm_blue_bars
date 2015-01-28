/*
 * Finder.cpp
 *
 *  Created on: 26.01.2015
 *      Author: chris
 */

#include "FrontierFinder.h"

#include <costmap_2d/costmap_2d.h>
#include <costmap_2d/cost_values.h>

#include <tf/tf.h>
#include <tf/LinearMath/Vector3.h>

#include <math.h>       /* atan2 */

namespace autonohm {
namespace frontier {
Finder::Finder(void) :
      _initialized(false)
{
   // start with no initialization
}

Finder::Finder(FinderConfig config) :
      _initialized(true)
    , _config(config)
{

}

Finder::~Finder(void)
{
   // nothing to do
}

void Finder::setMap(const nav_msgs::OccupancyGrid& map)
{
   _map = map;
}

void Finder::setConfig(FinderConfig config)
{
   _config = config;
}


void Finder::calculateFrontiers(void)
{
   // remove all old frontiers
   _frontiers.clear();
   _frontiers_weighted.clear();
   _frontier_layer.cells.clear();

   // set info to header
   _frontier_layer.header.frame_id = "/map";
   _frontier_layer.header.stamp    = ros::Time::now();
   _frontier_layer.cell_height     = _map.info.resolution;
   _frontier_layer.cell_width      = _map.info.resolution;

   const unsigned int w    = _map.info.width;
   const unsigned int size = _map.info.height * w;


   // copy to tmp array
   signed char* map = new signed char[size];
   for(unsigned int idx=0 ; idx<size ; idx++) {
      map[idx] = _map.data[idx];
   }

   /*
    * Find all frontiers
    */
   for (unsigned idx = 0; idx<size; idx++)
   {
      const bool valid_point = (map[idx] == FREE) ;

      if ((valid_point  && map)
                        && (((idx + 1 < size) && (map[idx + 1] == UNKNOWN))
                        ||  ((idx - 1 >= 0)   && (map[idx - 1] == UNKNOWN))
                        ||  ((idx + w < size) && (map[idx + w] == UNKNOWN))
                        ||  ((idx - w >= 0)   && (map[idx - w] == UNKNOWN))))
      {
         _map.data[idx] = -128;
         _frontier_layer.cells.push_back(this->getPointFromIndex(idx,
                                                                 w,
                                                                 _map.info.origin.position.x,
                                                                 _map.info.origin.position.y,
                                                                 _map.info.resolution));
      }
      else
      {
         _map.data[idx] = -127;
      }
   }


   // clean up frontiers on seperate rows of the map
   unsigned idx = _map.info.height - 1;
   for (unsigned int y = 0; y < _map.info.width; y++) {
      _map.data[idx] = -127;
      idx += _map.info.height;
   }

   // group frontiers
   int segment_id = 127;
   std::vector<std::vector<FrontierPoint> > segments;

   for (unsigned int i = 0; i < size; i++)
   {
      if (_map.data[i] == -128)
      {
         std::vector<int> neighbors;
         std::vector<FrontierPoint> segment;
         neighbors.push_back(i);

         while (neighbors.size() > 0)
         {
            unsigned int idx = neighbors.back();
            neighbors.pop_back();
            _map.data[idx] = segment_id;

            tf::Vector3 orientation(0, 0, 0);
            unsigned int c = 0;
            if ((idx + 1 < size) && (map[idx+1] == UNKNOWN)) {
               orientation += tf::Vector3(1, 0, 0); c++;
            }
            if ((idx-1 >= 0)   && (map[idx-1]   == UNKNOWN)) {
               orientation += tf::Vector3(-1, 0, 0); c++;
            }
            if ((idx+w < size) && (map[idx+w]   == UNKNOWN)) {
               orientation += tf::Vector3(0, 1, 0);  c++;
            }
            if ((idx-w >= 0)   && (map[idx-w]   == UNKNOWN)) {
               orientation += tf::Vector3(0, -1, 0); c++;
            }

            assert(c > 0);

            autonohm::FrontierPoint fp;
            fp.idx         = idx;
            fp.orientation = orientation / c;
            segment.push_back(fp);

            // check all 8 neighbors
            if (((idx - 1) > 0)                  && (_map.data[idx - 1]     == -128))
               neighbors.push_back(idx - 1);

            if (((idx + 1) < size)               && (_map.data[idx + 1]     == -128))
               neighbors.push_back(idx + 1);

            if (((idx - w) > 0)                  && (_map.data[idx - w]     == -128))
               neighbors.push_back(idx - w);

            if (((idx - w + 1) > 0)              && (_map.data[idx - w + 1] == -128))
               neighbors.push_back(idx - w + 1);

            if (((idx - w - 1) > 0)              && (_map.data[idx - w - 1] == -128))
               neighbors.push_back(idx - w - 1);

            if (((idx + w) < size)               && (_map.data[idx + w]     == -128))
               neighbors.push_back(idx + w);

            if (((idx + w + 1) < size)           && (_map.data[idx + w + 1] == -128))
               neighbors.push_back(idx + w + 1);

            if (((idx + w - 1) < size)           && (_map.data[idx + w - 1] == -128))
               neighbors.push_back(idx + w - 1);
         }

         segments.push_back(segment);
         segment_id--;
         if (segment_id < -127)
            break;
      }
   }

   delete [] map;

   int num_segments = 127 - segment_id;
   if (num_segments <= 0)
      return;

   ROS_DEBUG_STREAM("Found " << segments.size() << " frontieres. ");


   for (unsigned int i=0; i < segments.size(); i++)
   {

      std::vector<FrontierPoint>& segment = segments[i];
      uint fontierCells                   = segment.size();

      /*
       * Size check: can the robot pass the found frontier
       */
      if (fontierCells * _map.info.resolution < _config.robot_radius )
      {
         continue;
      }
      else
      {
         float x = 0;
         float y = 0;
         tf::Vector3 d(0, 0, 0);
         for (unsigned int j = 0; j < fontierCells; j++) {
            d += segment[j].orientation;

            unsigned int cellIdx = segment[j].idx;
            geometry_msgs::Point p = this->getPointFromIndex(cellIdx, _map.info.width);
            x += (float)(cellIdx % _map.info.width);
            y += (float)(cellIdx / _map.info.width);

         }

         d = d / fontierCells;

         Frontier f;
         f.position.x   = _map.info.origin.position.x + _map.info.resolution * (x / fontierCells);
         f.position.y   = _map.info.origin.position.y + _map.info.resolution * (y / fontierCells);
         f.position.z   = 0.0;
         f.orientation  = tf::createQuaternionMsgFromYaw(std::atan2(d.y(), d.x()));
         _frontiers.push_back(f);

         WeightedFrontier wf;
         wf.frontier = f;
         wf.size     = fontierCells; // * _map.info.resolution;
         _frontiers_weighted.push_back(wf);
      }
   }
}


geometry_msgs::Point Finder::getPointFromIndex(unsigned int idx, unsigned int width,
                                                       float originX,    float originY,
                                                       float resolution)
{
   geometry_msgs::Point p;
   p.x = static_cast<float>(idx % width) + originX + (_map.info.resolution / 2.0f);
   p.y = static_cast<float>(idx / width) + originY + (_map.info.resolution / 2.0f);
   p.z = 0;

   return p;
}

} /* namespace frontier */
} /* namespace autonohm */

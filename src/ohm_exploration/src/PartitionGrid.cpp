#include "PartitionGrid.h"

PartitionGrid::PartitionGrid(const nav_msgs::OccupancyGrid& map, const float cellsize)
{
    const Eigen::Vector2f min(map.info.origin.position.x, map.info.origin.position.y);
    const Eigen::Vector2f max(min.x() + map.info.width  * map.info.resolution,
                              min.y() + map.info.height * map.info.resolution);

    /* Generate o-->
                |
                v
    */
    for (float x = 0.0f; x <= max.x(); x += cellsize * 2.0f)
        for (float y = 0.0f; y <= max.y(); y += cellsize * 2.0f)
            _grid.push_back(Partition(Eigen::Vector2f(x, y), cellsize * 3.0f));


    /* Generate <--+o
                   |
                   v
    */
    for (float x = -cellsize * 2.0f; x >= min.x(); x -= cellsize * 2.0f)
        for (float y = 0.0f; y <= max.y(); y += cellsize * 2.0f)
            _grid.push_back(Partition(Eigen::Vector2f(x, y), cellsize * 3.0f));


    /* Generate ^
                |
                +-->
                o
    */
    for (float x = 0.0f; x <= max.x(); x += cellsize * 2.0f)
        for (float y = -cellsize * 2.0f; y >= min.y(); y -= cellsize * 2.0f)
            _grid.push_back(Partition(Eigen::Vector2f(x, y), cellsize * 3.0f));


    /* Generate ^
                |
             <--+
                 o
    */
    for (float x = -cellsize * 2.0f; x >= min.x(); x -= cellsize * 2.0f)
        for (float y = -cellsize * 2.0f; y >= min.y(); y -= cellsize * 2.0f)
            _grid.push_back(Partition(Eigen::Vector2f(x, y), cellsize * 3.0f));
}

void PartitionGrid::insert(std::vector<Target*>& targets)
{
    for (std::vector<Partition>::iterator partition(_grid.begin()); partition < _grid.end(); ++partition)
        partition->insert(targets);
}

Partition* PartitionGrid::selected(void)
{
    if (_selected < _grid.size())
        return &_grid[_selected];

    return 0;
}

void PartitionGrid::switchToNextPartition(void)
{
    unsigned int min = 0;

    for (unsigned int i = 0; i < _grid.size(); ++i)
        if (_grid[i].center().norm() < _grid[min].center().norm())
            min = i;

    _selected = min;
}

visualization_msgs::MarkerArray PartitionGrid::getMarkerMsg(void) const
{
    visualization_msgs::MarkerArray msg;

    for (std::vector<Partition>::const_iterator partition(_grid.begin()); partition < _grid.end(); ++partition)
        msg.markers.push_back(partition->getMarkerMsg());

    return msg;
}

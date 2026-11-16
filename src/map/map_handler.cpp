#include <map/map_handler.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

// Constructor: initializes and loads the map
MapHandler::MapHandler(const std::string &filename) : filename(filename)
{
    loadMap(filename);
    maxApothem =
        schemaVersion >= 2
            ? (cellNumber - 1) * cellSize / 2
            : cellNumber * cellSize / 2;
}

// Loads map data from a JSON file
void MapHandler::loadMap(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Cannot open JSON file: " << filename << std::endl;
        return;
    }

    try
    {
        file >> jsonData;

        if (jsonData.contains("map"))
        {
            map = jsonData["map"].get<std::vector<std::vector<float>>>();
        }
        else
        {
            std::cerr << "JSON key 'map' is missing" << std::endl;
        }

        if (jsonData.contains("schema_version"))
            schemaVersion = jsonData["schema_version"].get<int>();

        if (jsonData.contains("cell_size"))
        {
            cellSize = jsonData["cell_size"].get<float>();
        }
        else
        {
            std::cerr << "JSON key 'cell_size' is missing" << std::endl;
        }

        if (jsonData.contains("cell_number"))
        {
            cellNumber = jsonData["cell_number"].get<int>();
        }
        else
        {
            std::cerr << "JSON key 'cell_number' is missing" << std::endl;
        }

        if (schemaVersion >= 2)
        {
            if (cellNumber < 3 || cellNumber % 2 == 0)
                throw std::runtime_error(
                    "v2 map cell_number must be odd and at least 3");
            if (
                !jsonData.contains("metadata")
                || !jsonData["metadata"].contains("grid_origin")
                || jsonData["metadata"]["grid_origin"]
                       != "explicit-center-cell")
            {
                throw std::runtime_error(
                    "v2 map requires grid_origin=explicit-center-cell");
            }
        }

        if (
            jsonData.contains("metadata")
            && jsonData["metadata"].contains("o2i_model")
            && jsonData["metadata"]["o2i_model"] == "baked-geometric")
        {
            building_loss_baked = true;
        }

        if (jsonData.contains("space"))
        {
            space = jsonData["space"].get<std::vector<std::vector<int>>>();
            if (space.size() != map.size())
                throw std::runtime_error("space row count does not match the gain map");
            for (size_t row = 0; row < space.size(); ++row)
            {
                if (space[row].size() != map[row].size())
                    throw std::runtime_error("space grid is not the same shape as the gain map");
            }
        }
    }
    catch (json::parse_error &e)
    {
        std::cerr << "Error parsing JSON file: " << e.what() << std::endl;
    }
}

std::vector<std::vector<float>> MapHandler::getMap() const
{
    return map;
}

float MapHandler::getCellSize() const
{
    return cellSize;
}

int MapHandler::getCellNumber() const
{
    return cellNumber;
}

float MapHandler::getMaxApothem() const
{
    return maxApothem;
}

bool MapHandler::hasSpace() const
{
    return !space.empty();
}

bool MapHandler::buildingLossBaked() const
{
    return building_loss_baked;
}

int MapHandler::spaceAt(float x, float y) const
{
    if (space.empty())
        return -1;

    const float origin_index =
        schemaVersion >= 2
            ? (cellNumber - 1) / 2.0f
            : cellNumber / 2.0f;
    int x_index = (int)std::lround(x / cellSize + origin_index);
    int y_index = (int)std::lround(y / cellSize + origin_index);
    x_index = std::max(0, std::min(x_index, cellNumber - 1));
    y_index = std::max(0, std::min(y_index, cellNumber - 1));
    return space[y_index][x_index];
}

// Returns the interpolated shadow fading value at position (x, y)
float MapHandler::getMacroFadingValue(float x, float y) const
{
    return bilinearInterpolation(x, y);
}

// Performs bilinear interpolation over the shadow fading map
float MapHandler::bilinearInterpolation(float x, float y) const
{
    if (map.empty())
    {
        std::cerr << "The map is empty. Interpolation cannot be performed." << std::endl;
        return 0.0f;
    }

    int height = cellNumber;
    int width = height;

    const float origin_index =
        schemaVersion >= 2
            ? (width - 1) / 2.0f
            : width / 2.0f;
    float x_index = (x / cellSize) + origin_index;
    float y_index = (y / cellSize) + origin_index;

    int x0 = std::floor(x_index);
    int x1 = std::ceil(x_index);
    int y0 = std::floor(y_index);
    int y1 = std::ceil(y_index);

    x0 = std::max(0, std::min(x0, width - 1));
    x1 = std::max(0, std::min(x1, width - 1));
    y0 = std::max(0, std::min(y0, height - 1));
    y1 = std::max(0, std::min(y1, height - 1));

    if (x0 == x1 && y0 == y1)
        return map[y0][x0];

    if (x0 == x1)
        return ((y1 - y_index) * map[y0][x0] + (y_index - y0) * map[y1][x0]);

    if (y0 == y1)
        return ((x1 - x_index) * map[y0][x0] + (x_index - x0) * map[y0][x1]);

    float Q11 = map[y0][x0];
    float Q21 = map[y0][x1];
    float Q12 = map[y1][x0];
    float Q22 = map[y1][x1];

    float R1 = ((x1 - x_index) * Q11) + ((x_index - x0) * Q21);
    float R2 = ((x1 - x_index) * Q12) + ((x_index - x0) * Q22);
    float P = ((y1 - y_index) * R1) + ((y_index - y0) * R2);

    return P;
}

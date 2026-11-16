#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <cmath>

// Alias for convenience
using json = nlohmann::json;

class MapHandler {
public:
  
    
    MapHandler(const std::string& filename);
    
    std::vector<std::vector<float>> getMap() const;

    float getCellSize() const;
    int getCellNumber() const;
    float getMacroFadingValue(float x, float y) const;
    float getMaxApothem() const;

    // Site maps add a nearest-cell class: 0 open, 1 indoor, 2 roadway,
    // 3 impassable ground, for example water.
    // Catalog maps leave this empty. -1 means the map has no space grid.
    bool hasSpace() const;
    int spaceAt(float x, float y) const;
    // True when outdoor-to-indoor loss is already stored in the gain grid.
    bool buildingLossBaked() const;

private:
    // Private method to load the map from the JSON file
    void loadMap(const std::string &filename);
    
    // Private method for bilinear interpolation
    float bilinearInterpolation(float x, float y) const;

    // Member variables
    std::string filename;
    json jsonData;
    std::vector<std::vector<float>> map;
    std::vector<std::vector<int>> space;
    float cellSize;
    int cellNumber;  
    float maxApothem;
    int schemaVersion = 1;
    bool building_loss_baked = false;

   
};

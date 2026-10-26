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

private:
    // Private method to load the map from the JSON file
    void loadMap(const std::string &filename);
    
    // Private method for bilinear interpolation
    float bilinearInterpolation(float x, float y) const;

    // Member variables
    std::string filename;
    json jsonData;
    std::vector<std::vector<float>> map;
    float cellSize;
    int cellNumber;  
    float maxApothem;
    int schemaVersion = 1;

   
};

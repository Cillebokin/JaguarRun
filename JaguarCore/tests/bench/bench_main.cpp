#include <cstring>
#include <iostream>

int runGeometryBench();
int runCollisionBench();
int runLayoutBench();

int main(int argc, char* argv[])
{
    const char* selection = argc > 1 ? argv[1] : "all";
    if (std::strcmp(selection, "geometry") == 0)
        return runGeometryBench();
    if (std::strcmp(selection, "collision") == 0)
        return runCollisionBench();
    if (std::strcmp(selection, "layout") == 0)
        return runLayoutBench();
    if (std::strcmp(selection, "all") == 0)
    {
        const int geometry = runGeometryBench();
        const int collision = runCollisionBench();
        const int layout = runLayoutBench();
        return geometry != 0 ? geometry : (collision != 0 ? collision : layout);
    }

    std::cerr << "usage: JaguarCoreBench [geometry|collision|layout|all]\n";
    return 2;
}

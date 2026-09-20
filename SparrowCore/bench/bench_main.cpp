#include "SparrowCore/version.h"

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "all")
    {
        std::cout << "sparrowcore," << sparrow::version()
                  << ",empty,0,0,0,0,0,0,0,0\n";
        return 0;
    }
    std::cerr << "usage: SparrowCoreBench all\n";
    return 2;
}

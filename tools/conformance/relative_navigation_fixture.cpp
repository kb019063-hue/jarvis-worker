// A direct probe of the resolver, separate from the JavaScript URL API.
#include "litebird/net/ResourceLoader.hpp"
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::cout << litebird::ResourceLoader::resolve_url(argv[1], argv[2]);
}

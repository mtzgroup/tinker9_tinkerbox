cmake_minimum_required (VERSION 3.12)


add_library (tinker9_f OBJECT tinker_supplement.F)
target_link_libraries (tinker9_f PRIVATE tinker9_cpp)

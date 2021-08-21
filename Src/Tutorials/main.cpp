// vGPU.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include "Tutorials.h"

int main()
{
    if constexpr (false) if( auto err = T01_Example(); err ) return err;
    if constexpr (!false) if (auto err = T02_Example(); err) return err;

    return 0;
}

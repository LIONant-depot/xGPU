#include <iostream>
#include "xGPU.h"

int T01_Initialize()
{
    /*
    // initialize the VkApplicationInfo structure
    VkApplicationInfo AppInfo
    {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO
    ,   .pNext = nullptr
    ,   .pApplicationName = "vGPU-Tutorials"
    ,   .applicationVersion = 1
    ,   .pEngineName = "vGPU-Tutorials"
    ,   .engineVersion = 1
    ,   .apiVersion = VK_API_VERSION_1_0
    };

    // initialize the VkInstanceCreateInfo structure
    VkInstanceCreateInfo InstanceInfo
    {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
    ,   .pNext = nullptr
    ,   .flags = 0
    ,   .pApplicationInfo = &AppInfo
    ,   .enabledLayerCount = 0
    ,   .ppEnabledLayerNames = nullptr
    ,   .enabledExtensionCount = 0
    ,   .ppEnabledExtensionNames = nullptr
    };

    VkInstance VKInstance;

    if (auto Res = vkCreateInstance(&InstanceInfo, nullptr, &VKInstance); Res == VK_ERROR_INCOMPATIBLE_DRIVER)
    {
        std::cout << "cannot find a Vulkan Compatible Driver\n";
        return -1;
    }
    else if (Res)
    {
        std::cout << "unknown error\n";
        return -1;
    }


    vkDestroyInstance(VKInstance, nullptr);
    */
    return 0;
}

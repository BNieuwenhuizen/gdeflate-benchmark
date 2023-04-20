#include "format.h"

#include <vulkan/vulkan.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(x)                 \
   do {                          \
     VkResult result_ = (x);      \
     if (result_ != VK_SUCCESS) { \
       fprintf(stderr, #x " failed with error code %d\n", result_); \
       abort(); \
     }\
   } while(false);


static const uint32_t shader_spv[] = {
#include "gdeflate_shader.spv.h"
};

struct PushConstant {
  std::uint64_t tile_list_addr;
  std::uint64_t output_addr;
  std::uint64_t input_addr;
  std::uint32_t uncompressed_size;
  std::uint32_t reserved0;
};


void RecordDispatches(const FileHeader &header,
                      VkCommandBuffer cmdbuf,
                      VkPipeline pipeline,
                      VkPipelineLayout layout,
                      std::uint64_t tile_addr,
                      std::uint64_t input_addr,
                      std::uint64_t output_addr) {

  VkMemoryBarrier begin_mem_barrier = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
  };
  vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       0, 1, &begin_mem_barrier, 0, nullptr, 0, nullptr);
  
  vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

  for (unsigned i = 0; i < header.num_chunks; ++i) {
    std::size_t base_page = i* header.tiles_per_chunk;
    std::size_t num_pages = std::min<std::size_t>(header.tiles_per_chunk,
                                                  header.num_tiles - base_page);
    std::size_t out_offset = i * header.chunk_size;
    std::size_t out_size = std::min<std::size_t>(header.chunk_size,
                                                 header.uncompressed_size - out_offset);
    PushConstant args = {
      .tile_list_addr = tile_addr + base_page * sizeof(Tile),
      .output_addr = output_addr + out_offset,
      .input_addr = input_addr,
      .uncompressed_size = (std::uint32_t)out_size
    
    };

    vkCmdPushConstants(cmdbuf, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(args), &args);
    vkCmdDispatch(cmdbuf, num_pages, 1, 1);
  }
  
  VkMemoryBarrier end_mem_barrier = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
  };
  vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                       0, 1, &end_mem_barrier, 0, nullptr, 0, nullptr);
}

void CreatePipeline(VkDevice &dev, VkPipelineLayout &layout, VkPipeline &pipeline) {
  VkPushConstantRange push_range = {
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    .offset = 0,
    .size = sizeof(PushConstant)
  };
  VkPipelineLayoutCreateInfo pl_create_info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .pushConstantRangeCount = 1,
    .pPushConstantRanges = &push_range
  };
  CHECK(vkCreatePipelineLayout(dev, &pl_create_info, nullptr, &layout));
  
  VkShaderModuleCreateInfo shader_create_info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = sizeof(shader_spv),
    .pCode = shader_spv
  };
  
  VkShaderModule shader;
  CHECK(vkCreateShaderModule(dev, &shader_create_info, nullptr, &shader));
  
  VkComputePipelineCreateInfo pipeline_create_info = {
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = shader,
      .pName = "main",
    },
    .layout = layout,
  };
  CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline));
}


struct Buffer {
  std::uint64_t va;
  void *ptr;
  std::uint64_t size;
  
  VkDeviceMemory memory;
  VkBuffer buffer;
};

Buffer CreateBuffer(VkPhysicalDevice pdev, VkDevice dev, std::uint64_t size) {
  Buffer ret;
  
  VkBufferCreateInfo buffer_create_info = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = size,
    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  CHECK(vkCreateBuffer(dev, &buffer_create_info, nullptr, &ret.buffer));
  
  VkMemoryRequirements reqs;
  vkGetBufferMemoryRequirements(dev, ret.buffer, &reqs);
  
  VkMemoryAllocateInfo allocate_info = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = reqs.size,
    .memoryTypeIndex = 0,
  };
  
  VkPhysicalDeviceMemoryProperties props;
  vkGetPhysicalDeviceMemoryProperties(pdev, &props);
  
  for (unsigned i = 0; i < props.memoryTypeCount; ++i) {
    if (!((1u << i) & reqs.memoryTypeBits))
      continue;
    
    if ((props.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) != (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
      continue;
    
    allocate_info.memoryTypeIndex = i;
    break;
  }
  CHECK(vkAllocateMemory(dev, &allocate_info, nullptr, &ret.memory));
  
  vkBindBufferMemory(dev, ret.buffer, ret.memory, 0);

  CHECK(vkMapMemory(dev, ret.memory, 0, reqs.size, 0, &ret.ptr));
  
  VkBufferDeviceAddressInfo getaddr_info = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
    .buffer = ret.buffer,
  };

  ret.va = vkGetBufferDeviceAddress(dev, &getaddr_info);
  ret.size = reqs.size;
  return ret;
}



int main() {
  File file = ReadFile("t.bin");

  VkApplicationInfo app_info = {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName = "decompress-gpu",
    .apiVersion = VK_API_VERSION_1_3
  };
  VkInstanceCreateInfo instance_create_info = {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &app_info,
  };
  VkInstance instance;
  CHECK(vkCreateInstance(&instance_create_info, nullptr, &instance));
  
  
  VkPhysicalDevice pdev;
  std::uint32_t num_pdevs = 1;
  VkResult result = vkEnumeratePhysicalDevices(instance, &num_pdevs, &pdev);
  if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || num_pdevs == 0) {
    fprintf(stderr, "Failed to find a Vulkan device\n");
    return 1;
  }
  
  uint32_t num_queues = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(pdev, &num_queues, nullptr);
  
  std::vector<VkQueueFamilyProperties> queue_props(num_queues);
  vkGetPhysicalDeviceQueueFamilyProperties(pdev, &num_queues, queue_props.data());
  
  std::uint32_t qfi = UINT32_MAX;
  for (unsigned i = 0; i < num_queues; ++i) {
    if ((queue_props[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != VK_QUEUE_COMPUTE_BIT)
      continue;
    
    qfi = i;
    break;
  }
  
  if (qfi == UINT32_MAX) {
    fprintf(stderr, "Didn't find a compute queue\n");
    return 1;
  }

  const float queue_prio = 0.5;
  VkDeviceQueueCreateInfo queue_create_info = {
    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueFamilyIndex = qfi,
    .queueCount = 1,
    .pQueuePriorities = &queue_prio,
    
  };
  VkDeviceCreateInfo device_create_info = {
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &queue_create_info,
  };
  VkDevice dev;
  CHECK(vkCreateDevice(pdev, &device_create_info, nullptr, &dev));
  
  VkQueue queue;
  vkGetDeviceQueue(dev, qfi, 0, &queue);
  
  VkPipelineLayout layout;
  VkPipeline pipeline;
  CreatePipeline(dev, layout, pipeline);
  
  Buffer input_buffer = CreateBuffer(pdev, dev, file.data.size());
  Buffer output_buffer = CreateBuffer(pdev, dev, file.header.uncompressed_size);
  Buffer tile_buffer = CreateBuffer(pdev, dev, file.tiles.size() * sizeof(Tile));
  
  std::memcpy(input_buffer.ptr, file.data.data(), file.data.size());
  std::memcpy(tile_buffer.ptr, file.tiles.data(), file.tiles.size() * sizeof(Tile));
  
  VkCommandPoolCreateInfo cmd_pool_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .queueFamilyIndex = qfi,
  };
  
  VkCommandPool cmd_pool;
  CHECK(vkCreateCommandPool(dev, &cmd_pool_info, nullptr, &cmd_pool));
  
  VkCommandBufferAllocateInfo cmd_alloc_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = cmd_pool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1,
  };
  
  VkCommandBuffer cmd_buf;
  CHECK(vkAllocateCommandBuffers(dev, &cmd_alloc_info, &cmd_buf));

  VkCommandBufferBeginInfo cmd_begin_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };

  CHECK(vkBeginCommandBuffer(cmd_buf, &cmd_begin_info));
  
  RecordDispatches(file.header, cmd_buf, pipeline, layout, tile_buffer.va, input_buffer.va, output_buffer.va);
  CHECK(vkEndCommandBuffer(cmd_buf));
  
  VkFenceCreateInfo fence_info = {
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
  };

  VkFence fence;
  CHECK(vkCreateFence(dev, &fence_info, nullptr, &fence));
  
  VkSubmitInfo submit_info = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .commandBufferCount = 1,
    .pCommandBuffers = &cmd_buf,
  };
  
  for (;;) {
  unsigned iterations;
  auto start = std::chrono::steady_clock::now();
  for (iterations = 0;; ++iterations) {
    auto cur = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(cur - start).count() >= 10.0)
      break;

    CHECK(vkQueueSubmit(queue, 1, &submit_info, fence));
    CHECK(vkWaitForFences(dev, 1, &fence, true, 1'000'000'000));
    CHECK(vkResetFences(dev, 1, &fence));
  }
  auto end = std::chrono::steady_clock::now();
  std::chrono::duration<double> duration = end - start;
  
  std::size_t compressed_data = input_buffer.size * iterations;
  std::size_t uncompressed_data = output_buffer.size * iterations;
  printf("compressed throughput: %f\n", compressed_data/duration.count()/1e9);
  printf("uncompressed throughput: %f\n", uncompressed_data/duration.count()/1e9);
  }
  return 0;
}

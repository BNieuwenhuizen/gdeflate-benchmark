#include "libdeflate.h"

#include "format.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

std::atomic<std::uint64_t> compressed_data_processed; 
std::atomic<std::uint64_t> uncompressed_data_processed;

void ProcessChunk(libdeflate_gdeflate_decompressor *decompressor,
                  const File& file,
                  std::vector<libdeflate_gdeflate_in_page> &pages,
                  std::vector<char> &out_data,
                  unsigned chunk_id) {
  std::size_t base_page = chunk_id * file.header.tiles_per_chunk;
  std::size_t num_pages = std::min<std::size_t>(file.header.tiles_per_chunk,
                                                file.header.num_tiles - base_page);
  std::size_t out_offset = chunk_id * file.header.chunk_size;
  std::size_t out_size = std::min<std::size_t>(file.header.chunk_size,
                                               out_data.size() - out_offset);
  std::size_t in_size = 0;
  for (std::size_t i = 0; i < num_pages; ++i) {
    pages[base_page + i].data = file.data.data() + file.tiles[base_page + i].offset;
    pages[base_page + i].nbytes = file.tiles[base_page + i].size;
    in_size += file.tiles[base_page + i].size;
  }
  
  libdeflate_result result =
    libdeflate_gdeflate_decompress(decompressor, pages.data() + base_page, num_pages,
                                   out_data.data() + out_offset, out_size,
                                   NULL);
  if (result != LIBDEFLATE_SUCCESS) {
    fprintf(stderr, "decompress failed %d\n", result);
    abort();
  }
  
  compressed_data_processed += in_size;
  uncompressed_data_processed += out_size;
}

void RunThread(libdeflate_gdeflate_decompressor *decompressor,
               const File& file,
               std::vector<libdeflate_gdeflate_in_page> &pages,
               std::vector<char> &out_data,
               unsigned id, unsigned num_threads) {
  
  auto start = std::chrono::steady_clock::now();
  for (;;) {
    auto end = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(end - start).count() >= 10)
      return;
    for (unsigned i = id; i < file.header.num_chunks; i += num_threads) {
      ProcessChunk(decompressor, file, pages, out_data, i);
    }
  }
}

int main(int argc, char *argv[]) {
  File file = ReadFile("t.bin");
  compressed_data_processed = 0;
  uncompressed_data_processed = 0;
  
  libdeflate_gdeflate_decompressor *decompressor = libdeflate_alloc_gdeflate_decompressor();
  
  std::vector<char> out_data(file.header.uncompressed_size);
  std::vector<libdeflate_gdeflate_in_page> pages(file.header.num_tiles);
  
  unsigned num_threads = 1;
  if (argc == 2 && std::strcmp(argv[1], "-j") == 0)
    num_threads = std::thread::hardware_concurrency();

  for (;;) {
  std::vector<std::thread> threads;
  
  auto start = std::chrono::steady_clock::now();
  
  for (unsigned i = 0; i < num_threads; ++i) {
    threads.emplace_back([&,i]() { RunThread(decompressor, file, pages, out_data, i, num_threads); });
  }
  
  for (unsigned i = 0; i < num_threads; ++i) {
    threads[i].join();
  }
  
  for (std::size_t i = 0; i < out_data.size(); ++i) {
    if (out_data[i] != (i & 127)) {
      fprintf(stderr, "invalid data at %zu\n", i);
      return 1;
    }
  }
  
  auto end = std::chrono::steady_clock::now();
  std::chrono::duration<double> duration = end - start;
  
  std::uint64_t compressed_data_processed_snapshot = compressed_data_processed;
  std::uint64_t uncompressed_data_processed_snapshot = uncompressed_data_processed;
  printf("results: time: %f uncompressed data=%f GB compressed data=%f GB\n", duration.count(),
         uncompressed_data_processed_snapshot/1e9, compressed_data_processed_snapshot/1e9);
  printf("compressed throughput: %f\n", compressed_data_processed_snapshot/duration.count()/1e9);
  printf("uncompressed throughput: %f\n", uncompressed_data_processed_snapshot/duration.count()/1e9);
  }

  libdeflate_free_gdeflate_decompressor(decompressor);
  
  return 0;
}

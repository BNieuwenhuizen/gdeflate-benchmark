#include "libdeflate.h"

#include "format.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <vector>

void compress_buffer(std::span<char> in_data, std::size_t chunk_size = 1024u * 1024u * 16u) {
  struct libdeflate_gdeflate_compressor *compressor =
    libdeflate_alloc_gdeflate_compressor(6);
  
  size_t num_chunks = (in_data.size() + chunk_size - 1) / chunk_size;
  size_t max_pages;
  size_t max_size = libdeflate_gdeflate_compress_bound(compressor, chunk_size, &max_pages);
  
  FileHeader header;
  header.uncompressed_size = in_data.size();
  header.compressed_size = 0;
  header.chunk_size = chunk_size;
  header.tiles_per_chunk = max_pages;
  header.num_chunks = num_chunks;
  
  std::vector<char> out_data;
  std::vector<Tile> out_tiles;
  
  size_t max_page_size = max_size / max_pages;
  
  std::vector<char> tmp_buffer(max_size);
  std::vector<libdeflate_gdeflate_out_page> pages(max_pages);
  for (unsigned i = 0; i < num_chunks; ++i) {
    std::size_t cur_chunk_size = std::min<std::size_t>(chunk_size, in_data.size() - chunk_size * i);
  
    size_t max_pages;
    size_t max_size = libdeflate_gdeflate_compress_bound(compressor, cur_chunk_size, &max_pages);
    tmp_buffer.resize(max_size);

    for (std::size_t j = 0; j < max_pages; ++j) {
      pages[j].data = tmp_buffer.data() + j * max_page_size,
      pages[j].nbytes = max_page_size;
    }
    
    libdeflate_gdeflate_compress(compressor, in_data.data() + i * chunk_size,
                                 cur_chunk_size, pages.data(), max_pages);
    for (std::size_t j = 0; j < max_pages; ++j) {
      out_tiles.push_back(Tile{(std::uint32_t)out_data.size(), (std::uint32_t)pages[j].nbytes});
      std::copy_n((const char*)pages[j].data, pages[j].nbytes, std::back_inserter(out_data));
    }
  }
  
  header.num_tiles = (std::uint32_t)out_tiles.size();
  header.compressed_size = out_data.size();

  FILE *f = fopen("t.bin", "wb");
  fwrite(&header, sizeof(header), 1, f);
  fwrite(out_tiles.data(), sizeof(Tile), out_tiles.size(), f);
  fwrite(out_data.data(), out_data.size(), 1, f);
  fclose(f);

  printf("compression results: uncompressed size=%zu raw compressed size=%zu result size=%zu\n",
         in_data.size(), out_data.size(), (std::size_t)(out_data.size() + out_tiles.size() * sizeof(Tile) + sizeof(header)));
  libdeflate_free_gdeflate_compressor(compressor);
}


int main() {
  std::vector<char> data(1024 * 1024 * 1024, ' ');
  for (std::size_t i = 0; i < data.size(); ++i)
    data[i] = i & 127;

  compress_buffer(data);
  return 0;
}

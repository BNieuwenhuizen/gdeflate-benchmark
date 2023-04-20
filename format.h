#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>


struct FileHeader {
  std::uint32_t uncompressed_size;
  std::uint32_t compressed_size;
  std::uint32_t chunk_size;
  std::uint32_t tiles_per_chunk;
  std::uint32_t num_tiles;
  std::uint32_t num_chunks;
};

struct Tile {
  std::uint32_t offset;
  std::uint32_t size;
};

struct File {
  FileHeader header;
  std::vector<Tile> tiles;
  std::vector<char> data;
};


static inline File ReadFile(const char *filename) {
  FILE *f = fopen(filename, "rb");
  File file;
  fread(&file.header, sizeof(file.header), 1, f);
  file.tiles.resize(file.header.num_tiles);
  file.data.resize(file.header.compressed_size);
  
  fread(file.tiles.data(), sizeof(Tile), file.header.num_tiles, f);
  fread(file.data.data(), file.data.size(), 1, f);

  fclose(f);
  return file;
}

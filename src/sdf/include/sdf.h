// Copyright 2022 The Manifold Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "public.h"
#include "utils.h"
#include "vec_dh.h"

namespace {
using namespace manifold;

typedef unsigned long long int Uint64;

constexpr Uint64 kOpen = std::numeric_limits<Uint64>::max();

__host__ __device__ Uint64 AtomicCAS(Uint64& target, Uint64 compare,
                                     Uint64 val) {
#ifdef __CUDA_ARCH__
  return atomicCAS(&target, compare, val);
#else
  std::atomic<Uint64>& tar = reinterpret_cast<std::atomic<Uint64>&>(target);
  tar.compare_exchange_strong(compare, val);
  return compare;
#endif
}

__host__ __device__ int Next3(int i) {
  constexpr glm::ivec3 next3(1, 2, 0);
  return next3[i];
}

__host__ __device__ int Prev3(int i) {
  constexpr glm::ivec3 prev3(2, 0, 1);
  return prev3[i];
}

__host__ __device__ glm::ivec3 TetTri0(int i) {
  constexpr glm::ivec3 tetTri0[16] = {{-1, -1, -1},  //
                                      {0, 3, 4},     //
                                      {0, 1, 5},     //
                                      {1, 5, 3},     //
                                      {1, 4, 2},     //
                                      {1, 0, 3},     //
                                      {2, 5, 0},     //
                                      {5, 3, 2},     //
                                      {2, 3, 5},     //
                                      {0, 5, 2},     //
                                      {3, 0, 1},     //
                                      {2, 4, 1},     //
                                      {3, 5, 1},     //
                                      {5, 1, 0},     //
                                      {4, 3, 0},     //
                                      {-1, -1, -1}};
  return tetTri0[i];
}

__host__ __device__ glm::ivec3 TetTri1(int i) {
  constexpr glm::ivec3 tetTri1[16] = {{-1, -1, -1},  //
                                      {-1, -1, -1},  //
                                      {-1, -1, -1},  //
                                      {3, 4, 1},     //
                                      {-1, -1, -1},  //
                                      {3, 2, 1},     //
                                      {0, 4, 2},     //
                                      {-1, -1, -1},  //
                                      {-1, -1, -1},  //
                                      {2, 4, 0},     //
                                      {1, 2, 3},     //
                                      {-1, -1, -1},  //
                                      {1, 4, 3},     //
                                      {-1, -1, -1},  //
                                      {-1, -1, -1},  //
                                      {-1, -1, -1}};
  return tetTri1[i];
}

__host__ __device__ glm::ivec4 Neighbors(int i) {
  constexpr glm::ivec4 neighbors[7] = {{0, 0, 0, 1},   //
                                       {1, 0, 0, 0},   //
                                       {0, 1, 0, 0},   //
                                       {0, 0, 1, 0},   //
                                       {-1, 0, 0, 1},  //
                                       {0, -1, 0, 1},  //
                                       {0, 0, -1, 1}};
  return neighbors[i];
}

__host__ __device__ Uint64 SpreadBits3(Uint64 v) {
  v = v & 0x1fffff;
  v = (v | v << 32) & 0x1f00000000ffff;
  v = (v | v << 16) & 0x1f0000ff0000ff;
  v = (v | v << 8) & 0x100f00f00f00f00f;
  v = (v | v << 4) & 0x10c30c30c30c30c3;
  v = (v | v << 2) & 0x1249249249249249;
  return v;
}

__host__ __device__ Uint64 SqueezeBits3(Uint64 v) {
  v = v & 0x1249249249249249;
  v = (v ^ v >> 2) & 0x10c30c30c30c30c3;
  v = (v ^ v >> 4) & 0x100f00f00f00f00f;
  v = (v ^ v >> 8) & 0x1f0000ff0000ff;
  v = (v ^ v >> 16) & 0x1f00000000ffff;
  v = (v ^ v >> 32) & 0x1fffff;
  return v;
}

// This is a modified 3D MortonCode, where the xyz code is shifted by one bit
// and the w bit is added as the least significant. This allows 21 bits per x,
// y, and z channel and 1 for w, filling the 64 bit total.
__host__ __device__ Uint64 MortonCode(const glm::ivec4& index) {
  return static_cast<Uint64>(index.w) | (SpreadBits3(index.x) << 1) |
         (SpreadBits3(index.y) << 2) | (SpreadBits3(index.z) << 3);
}

__host__ __device__ glm::ivec4 DecodeMorton(Uint64 code) {
  glm::ivec4 index;
  index.x = SqueezeBits3(code >> 1);
  index.y = SqueezeBits3(code >> 2);
  index.z = SqueezeBits3(code >> 3);
  index.w = code & 0x1u;
  return index;
}

struct GridVert {
  Uint64 key = kOpen;
  float distance = NAN;
  int edgeVerts[7] = {-1, -1, -1, -1, -1, -1, -1};

  __host__ __device__ int Inside() const { return distance > 0 ? 1 : -1; }

  __host__ __device__ int NeighborInside(int i) const {
    return Inside() * (edgeVerts[i] < 0 ? 1 : -1);
  }
};

class HashTableD {
 public:
  HashTableD(VecDH<GridVert>& alloc, VecDH<uint32_t>& used, uint32_t step = 127)
      : step_{step}, table_{alloc}, used_{used} {}

  __host__ __device__ int Size() const { return table_.size(); }

  __host__ __device__ bool Full() const { return used_[0] * 2 > Size(); }

  __host__ __device__ void Insert(const GridVert& vert) {
    uint32_t idx = vert.key & (Size() - 1);
    while (1) {
      Uint64& key = table_[idx].key;
      const Uint64 found = AtomicCAS(key, kOpen, vert.key);
      if (found == kOpen) {
        AtomicAdd(used_[0], 0x1u);
        table_[idx] = vert;
        return;
      }
      if (found == vert.key) return;
      idx = (idx + step_) & (Size() - 1);
    }
  }

  __host__ __device__ GridVert operator[](Uint64 key) const {
    uint32_t idx = key & (Size() - 1);
    while (1) {
      const GridVert found = table_[idx];
      if (found.key == key || found.key == kOpen) return found;
      idx = (idx + step_) & (Size() - 1);
    }
  }

  __host__ __device__ GridVert At(int idx) const { return table_[idx]; }

 private:
  uint32_t step_;
  VecD<GridVert> table_;
  VecD<uint32_t> used_;
};

class HashTable {
 public:
  HashTable(uint32_t size, uint32_t step = 127)
      : alloc_{1 << (int)ceil(log2(size)), {}}, table_{alloc_, used_, step} {}

  HashTableD D() { return table_; }

  int Entries() const { return used_[0]; }

  int Size() const { return table_.Size(); }

  bool Full() const { return used_[0] * 2 > Size(); }

  float FilledFraction() const { return static_cast<float>(used_[0]) / Size(); }

 private:
  VecDH<GridVert> alloc_;
  VecDH<uint32_t> used_ = VecDH<uint32_t>(1, 0);
  HashTableD table_;
};

template <typename Func>
struct ComputeVerts {
  glm::vec3* vertPos;
  int* vertIndex;
  HashTableD gridVerts;
  const Func sdf;
  const glm::vec3 origin;
  const glm::ivec3 gridSize;
  const glm::vec3 spacing;
  const float level;

  inline __host__ __device__ glm::vec3 Position(glm::ivec4 gridIndex) const {
    return origin +
           spacing * (glm::vec3(gridIndex) + (gridIndex.w == 1 ? 0.0f : -0.5f));
  }

  inline __host__ __device__ float BoundedSDF(glm::ivec4 gridIndex) const {
    const float d = sdf(Position(gridIndex)) - level;

    const glm::ivec3 xyz(gridIndex);
    const bool onLowerBound = glm::any(glm::lessThanEqual(xyz, glm::ivec3(0)));
    const bool onUpperBound = glm::any(glm::greaterThanEqual(xyz, gridSize));
    const bool onHalfBound =
        gridIndex.w == 1 && glm::any(glm::greaterThanEqual(xyz, gridSize - 1));
    if (onLowerBound || onUpperBound || onHalfBound) return glm::min(d, 0.0f);

    return d;
  }

  inline __host__ __device__ void operator()(Uint64 mortonCode) {
    if (gridVerts.Full()) return;

    const glm::ivec4 gridIndex = DecodeMorton(mortonCode);

    if (glm::any(glm::greaterThan(glm::ivec3(gridIndex), gridSize))) return;

    const glm::vec3 position = Position(gridIndex);

    GridVert gridVert;
    gridVert.key = mortonCode;
    gridVert.distance = BoundedSDF(gridIndex);

    bool keep = false;
    // These seven edges are uniquely owned by this gridVert; any of them
    // which intersect the surface create a vert.
    for (int i = 0; i < 7; ++i) {
      glm::ivec4 neighborIndex = gridIndex + Neighbors(i);
      if (neighborIndex.w == 2) {
        neighborIndex += 1;
        neighborIndex.w = 0;
      }
      const float val = BoundedSDF(neighborIndex);
      if ((val > 0) == (gridVert.distance > 0)) continue;
      keep = true;

      const int idx = AtomicAdd(*vertIndex, 1);
      vertPos[idx] =
          (val * position - gridVert.distance * Position(neighborIndex)) /
          (val - gridVert.distance);
      gridVert.edgeVerts[i] = idx;
    }

    if (keep) gridVerts.Insert(gridVert);
  }
};

struct BuildTris {
  glm::ivec3* triVerts;
  int* triIndex;
  const HashTableD gridVerts;

  __host__ __device__ void CreateTri(const glm::ivec3& tri,
                                     const int edges[6]) {
    if (tri[0] < 0) return;
    int idx = AtomicAdd(*triIndex, 1);
    triVerts[idx] = {edges[tri[0]], edges[tri[1]], edges[tri[2]]};
  }

  __host__ __device__ void CreateTris(const glm::ivec4& tet,
                                      const int edges[6]) {
    const int i = (tet[0] > 0 ? 1 : 0) + (tet[1] > 0 ? 2 : 0) +
                  (tet[2] > 0 ? 4 : 0) + (tet[3] > 0 ? 8 : 0);
    CreateTri(TetTri0(i), edges);
    CreateTri(TetTri1(i), edges);
  }

  __host__ __device__ void operator()(int idx) {
    const GridVert& base = gridVerts.At(idx);
    if (base.key == kOpen) return;

    const glm::ivec4 baseIndex = DecodeMorton(base.key);

    glm::ivec4 leadIndex = baseIndex;
    if (leadIndex.w == 0)
      leadIndex.w = 1;
    else {
      leadIndex += 1;
      leadIndex.w = 0;
    }

    // This GridVert is in charge of the 6 tetrahedra surrounding its edge in
    // the (1,1,1) direction (edge 0).
    glm::ivec4 tet(base.NeighborInside(0), base.Inside(), -2, -2);
    glm::ivec4 thisIndex = baseIndex;
    thisIndex.x += 1;

    GridVert thisVert = gridVerts[MortonCode(thisIndex)];

    tet[2] = base.NeighborInside(1);
    for (const int i : {0, 1, 2}) {
      thisIndex = leadIndex;
      --thisIndex[Prev3(i)];
      // MortonCodes take unsigned input, so check for negatives, given the
      // decrement.
      GridVert nextVert = thisIndex[Prev3(i)] < 0
                              ? GridVert()
                              : gridVerts[MortonCode(thisIndex)];
      tet[3] = base.NeighborInside(Prev3(i) + 4);

      const int edges1[6] = {base.edgeVerts[0],
                             base.edgeVerts[i + 1],
                             nextVert.edgeVerts[Next3(i) + 4],
                             nextVert.edgeVerts[Prev3(i) + 1],
                             thisVert.edgeVerts[i + 4],
                             base.edgeVerts[Prev3(i) + 4]};
      thisVert = nextVert;
      CreateTris(tet, edges1);

      thisIndex = baseIndex;
      ++thisIndex[Next3(i)];
      nextVert = gridVerts[MortonCode(thisIndex)];
      tet[2] = tet[3];
      tet[3] = base.NeighborInside(Next3(i) + 1);

      const int edges2[6] = {base.edgeVerts[0],
                             edges1[5],
                             thisVert.edgeVerts[i + 4],
                             nextVert.edgeVerts[Next3(i) + 4],
                             edges1[3],
                             base.edgeVerts[Next3(i) + 1]};
      thisVert = nextVert;
      CreateTris(tet, edges2);

      tet[2] = tet[3];
    }
  }
};
}  // namespace

namespace manifold {

/** @addtogroup Core
 *  @{
 */

/**
 * Constructs a level-set Mesh from the input Signed-Distance Function (SDF).
 * This uses a form of Marching Tetrahedra (akin to Marching Cubes, but better
 * for manifoldness). Instead of using a cubic grid, it uses a body-centered
 * cubic grid (two shifted cubic grids). This means if your function's interior
 * exceeds the given bounds, you will see a kind of egg-crate shape closing off
 * the manifold, which is due to the underlying grid.
 *
 * @param sdf The signed-distance functor, containing this function signature:
 * `__host__ __device__ float operator()(glm::vec3 point)`, which returns the
 * signed distance of a given point in R^3. Positive values are inside,
 * negative outside. The `__host__ __device__` is only needed if you compile for
 * CUDA. If you are using a large grid, the advantage of a GPU speedup is
 * quite significant.
 * @param bounds An axis-aligned box that defines the extent of the grid.
 * @param edgeLength Approximate maximum edge length of the triangles in the
 * final result. This affects grid spacing, and hence has a strong effect on
 * performance.
 * @param level You can inset your Mesh by using a positive value, or outset
 * it with a negative value.
 * @return Mesh This class does not depend on Manifold, so it just returns a
 * Mesh, but it is guaranteed to be manifold and so can always be used as
 * input to the Manifold constructor for further operations.
 */
template <typename Func>
inline Mesh LevelSet(Func sdf, Box bounds, float edgeLength, float level = 0) {
  Mesh out;

  const glm::vec3 dim = bounds.Size();
  const float maxDim = std::max(dim[0], std::max(dim[1], dim[2]));
  const glm::ivec3 gridSize(dim / edgeLength);
  const glm::vec3 spacing = dim / (glm::vec3(gridSize));

  const Uint64 maxMorton = MortonCode(glm::ivec4(gridSize + 1, 1));
  const auto policy = autoPolicy(maxMorton);

  int tableSize = glm::min(
      2 * maxMorton, static_cast<Uint64>(10 * glm::pow(maxMorton, 0.667)));
  HashTable gridVerts(tableSize);
  VecDH<glm::vec3> vertPos(gridVerts.Size() * 7);

  while (1) {
    VecDH<int> index(1, 0);
    for_each_n(
        policy, countAt(0), maxMorton + 1,
        ComputeVerts<Func>({vertPos.ptrD(), index.ptrD(), gridVerts.D(), sdf,
                            bounds.min, gridSize + 1, spacing, level}));

    if (gridVerts.Full()) {  // Resize HashTable
      const glm::vec3 lastVert = vertPos[index[0] - 1];
      const Uint64 lastMorton =
          MortonCode(glm::ivec4((lastVert - bounds.min) / spacing, 1));
      const float ratio = static_cast<float>(maxMorton) / lastMorton;
      if (ratio > 1000)  // do not trust the ratio if it is too large
        tableSize *= 2;
      else
        tableSize *= ratio;
      gridVerts = HashTable(tableSize);
      vertPos = VecDH<glm::vec3>(gridVerts.Size() * 7);
    } else {  // Success
      vertPos.resize(index[0]);
      break;
    }
  }

  VecDH<glm::ivec3> triVerts(gridVerts.Entries() * 12);  // worst case

  VecDH<int> index(1, 0);
  for_each_n(policy, countAt(0), gridVerts.Size(),
             BuildTris({triVerts.ptrD(), index.ptrD(), gridVerts.D()}));
  triVerts.resize(index[0]);

  out.vertPos.insert(out.vertPos.end(), vertPos.begin(), vertPos.end());
  out.triVerts.insert(out.triVerts.end(), triVerts.begin(), triVerts.end());
  return out;
}
/** @} */
}  // namespace manifold
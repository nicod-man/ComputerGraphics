//
// Implementation for Yocto/RayTrace.
//

//
// LICENSE:
//
// Copyright (c) 2016 -- 2020 Fabio Pellacini
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include "yocto_raytrace.h"

#include <yocto/yocto_color.h>
#include <yocto/yocto_geometry.h>
#include <yocto/yocto_math.h>
#include <yocto/yocto_parallel.h>
#include <yocto/yocto_shading.h>

// -----------------------------------------------------------------------------
// IMPLEMENTATION FOR SCENE EVALUATION
// -----------------------------------------------------------------------------
namespace yocto {

// Check texture size
static vec2i texture_size(const raytrace_texture* texture) {
  if (!texture->hdr.empty()) {
    return texture->hdr.imsize();
  } else if (!texture->ldr.empty()) {
    return texture->ldr.imsize();
  } else {
    return zero2i;
  }
}

// Evaluate a texture
static vec4f lookup_texture(const raytrace_texture* texture, const vec2i& ij,
    bool ldr_as_linear = false) {
  if (!texture->hdr.empty()) {
    return texture->hdr[ij];
  } else if (!texture->ldr.empty()) {
    return ldr_as_linear ? byte_to_float(texture->ldr[ij])
                         : srgb_to_rgb(byte_to_float(texture->ldr[ij]));
  } else {
    return {1, 1, 1, 1};
  }
}

// Evaluate a texture
static vec4f eval_texture(const raytrace_texture* texture, const vec2f& uv,
    bool ldr_as_linear = false, bool no_interpolation = false,
    bool clamp_to_edge = false) {
  // get texture
  if (!texture) return {1, 1, 1};

  // get yimg::image width/height
  auto size = texture_size(texture);

  // get coordinates normalized for tiling
  auto s = 0.0f, t = 0.0f;
  if (clamp_to_edge) {
    s = clamp(uv.x, 0.0f, 1.0f) * size.x;
    t = clamp(uv.y, 0.0f, 1.0f) * size.y;
  } else {
    s = fmod(uv.x, 1.0f) * size.x;
    if (s < 0) s += size.x;
    t = fmod(uv.y, 1.0f) * size.y;
    if (t < 0) t += size.y;
  }

  // get yimg::image coordinates and residuals
  auto i = clamp((int)s, 0, size.x - 1), j = clamp((int)t, 0, size.y - 1);
  auto ii = (i + 1) % size.x, jj = (j + 1) % size.y;
  auto u = s - i, v = t - j;

  if (no_interpolation) return lookup_texture(texture, {i, j}, ldr_as_linear);

  // handle interpolation
  return lookup_texture(texture, {i, j}, ldr_as_linear) * (1 - u) * (1 - v) +
         lookup_texture(texture, {i, jj}, ldr_as_linear) * (1 - u) * v +
         lookup_texture(texture, {ii, j}, ldr_as_linear) * u * (1 - v) +
         lookup_texture(texture, {ii, jj}, ldr_as_linear) * u * v;
}

// Generates a ray from a camera for yimg::image plane coordinate uv and
// the lens coordinates luv.
static ray3f eval_camera(const raytrace_camera* camera, const vec2f& image_uv) {
  // YOUR CODE GOES HERE -----------------------

  // It is modeled as an ideal pinhole camera. Therefore a ray is defined as:
  //                   r(u,v) = { o, (q-o / |q-o|) }.

  auto q = vec3f{camera->film.x * (0.5f - image_uv.x),
      camera->film.y * (image_uv.y - 0.5f), camera->lens};

  auto e = vec3f{0};
  // -q since we have switched the signs when computing q
  auto d = normalize(-q - e);
  return ray3f{
      transform_point(camera->frame, e), transform_direction(camera->frame, d)};

  // return ray3f{1,1,1};
}

// Eval position
static vec3f eval_position(
    const raytrace_shape* shape, int element, const vec2f& uv) {
  if (!shape->triangles.empty()) {
    auto t = shape->triangles[element];
    return interpolate_triangle(shape->positions[t.x], shape->positions[t.y],
        shape->positions[t.z], uv);
  } else if (!shape->lines.empty()) {
    auto l = shape->lines[element];
    return interpolate_line(shape->positions[l.x], shape->positions[l.y], uv.x);
  } else if (!shape->points.empty()) {
    return shape->positions[shape->points[element]];
  } else {
    return zero3f;
  }
}

// Shape element normal.
static vec3f eval_element_normal(const raytrace_shape* shape, int element) {
  // YOUR CODE GOES HERE -----------------------
  // When we miss normals:
  if (!shape->triangles.empty()) {
    // triangle case: just return the triangle face normal
    auto t = shape->triangles[element];
    return triangle_normal(
        shape->positions[t.x], shape->positions[t.y], shape->positions[t.z]);
  } else {
    // line case: just return the tangent to the line
    auto l = shape->lines[element];
    return line_tangent(shape->positions[l.x], shape->positions[l.y]);
  }
}

// Eval normal
static vec3f eval_normal(
    const raytrace_shape* shape, int element, const vec2f& uv) {
  // YOUR CODE GOES HERE -----------------------
  if (shape->normals.empty()) return eval_element_normal(shape, element);

  if (!shape->triangles.empty()) {
    auto t = shape->triangles[element];
    return normalize(interpolate_triangle(
        shape->normals[t.x], shape->normals[t.y], shape->normals[t.z], uv));
  } else if (!shape->lines.empty()) {
    auto l = shape->lines[element];
    return normalize(
        interpolate_line(shape->normals[l.x], shape->normals[l.y], uv.x));
  } else if (!shape->points.empty()) {
    return shape->normals[shape->points[element]];
  } else {
    return zero3f;
  }
}

// Eval texcoord
static vec2f eval_texcoord(
    const raytrace_shape* shape, int element, const vec2f& uv) {
  // YOUR CODE GOES HERE -----------------------

  // I was gettin core dump in cornelbox and ecosystem. I figured out that was
  // due to null pointer when it was trying to give texture even if there was no
  // texture at all!
  if (shape->texcoords.empty()) return uv;

  if (!shape->triangles.empty()) {
    auto t = shape->triangles[element];
    return interpolate_triangle(shape->texcoords[t.x], shape->texcoords[t.y],
        shape->texcoords[t.z], uv);
  } else if (!shape->lines.empty()) {
    auto l = shape->lines[element];
    return interpolate_line(shape->texcoords[l.x], shape->texcoords[l.y], uv.x);
  } else if (!shape->points.empty()) {
    return shape->texcoords[shape->points[element]];
  } else {
    return zero2f;
  }
}

// Evaluate all environment color.
static vec3f eval_environment(const raytrace_scene* scene, const ray3f& ray) {
  // YOUR CODE GOES HERE -----------------------
  auto radiance = zero3f;
  for (auto env : scene->environments) {
    auto env_radiance = env->emission;
    if (env->emission_tex) {
      // transform world coord to local one
      auto local_dir = transform_direction(inverse(env->frame), ray.d);
      auto texcoord  = vec2f{atan2(local_dir.z, local_dir.x) / (2 * pif),
          acos(clamp(local_dir.y, -1.0f, 1.0f)) / pif};
      if (texcoord.x < 0) texcoord.x += 1;
      env_radiance *= xyz(eval_texture(env->emission_tex, texcoord));
    }
    radiance += env_radiance;
  }
  return radiance;
}
}  // namespace yocto

// -----------------------------------------------------------------------------
// IMPLEMENTATION FOR SHAPE/SCENE BVH
// -----------------------------------------------------------------------------
namespace yocto {

// primitive used to sort bvh entries
struct raytrace_bvh_primitive {
  bbox3f bbox      = invalidb3f;
  vec3f  center    = zero3f;
  int    primitive = 0;
};

// Splits a BVH node. Returns split position and axis.
static pair<int, int> split_middle(
    vector<raytrace_bvh_primitive>& primitives, int start, int end) {
  // initialize split axis and position
  auto axis = 0;
  auto mid  = (start + end) / 2;

  // compute primintive bounds and size
  auto cbbox = invalidb3f;
  for (auto i = start; i < end; i++) cbbox = merge(cbbox, primitives[i].center);
  auto csize = cbbox.max - cbbox.min;
  if (csize == zero3f) return {mid, axis};

  // split along largest
  if (csize.x >= csize.y && csize.x >= csize.z) axis = 0;
  if (csize.y >= csize.x && csize.y >= csize.z) axis = 1;
  if (csize.z >= csize.x && csize.z >= csize.y) axis = 2;

  // split the space in the middle along the largest axis
  mid = (int)(std::partition(primitives.data() + start, primitives.data() + end,
                  [axis, middle = center(cbbox)[axis]](auto& primitive) {
                    return primitive.center[axis] < middle;
                  }) -
              primitives.data());

  // if we were not able to split, just break the primitives in half
  if (mid == start || mid == end) {
    // throw runtime_error("bad bvh split");
    mid = (start + end) / 2;
  }

  return {mid, axis};
}

// Maximum number of primitives per BVH node.
const int bvh_max_prims = 4;

// Build BVH nodes
static void build_bvh(vector<raytrace_bvh_node>& nodes,
    vector<raytrace_bvh_primitive>&              primitives) {
  // prepare to build nodes
  nodes.clear();
  nodes.reserve(primitives.size() * 2);

  // queue up first node
  auto queue = std::deque<vec3i>{{0, 0, (int)primitives.size()}};
  nodes.emplace_back();

  // create nodes until the queue is empty
  while (!queue.empty()) {
    // grab node to work on
    auto next = queue.front();
    queue.pop_front();
    auto nodeid = next.x, start = next.y, end = next.z;

    // grab node
    auto& node = nodes[nodeid];

    // compute bounds
    node.bbox = invalidb3f;
    for (auto i = start; i < end; i++)
      node.bbox = merge(node.bbox, primitives[i].bbox);

    // split into two children
    if (end - start > bvh_max_prims) {
      // get split
      auto [mid, axis] = split_middle(primitives, start, end);

      // make an internal node
      node.internal = true;
      node.axis     = axis;
      node.num      = 2;
      node.start    = (int)nodes.size();
      nodes.emplace_back();
      nodes.emplace_back();
      queue.push_back({node.start + 0, start, mid});
      queue.push_back({node.start + 1, mid, end});
    } else {
      // Make a leaf node
      node.internal = false;
      node.num      = end - start;
      node.start    = start;
    }
  }

  // cleanup
  nodes.shrink_to_fit();
}

static void init_bvh(raytrace_shape* shape, const raytrace_params& params) {
  // build primitives
  auto primitives = vector<raytrace_bvh_primitive>{};
  if (!shape->points.empty()) {
    for (auto idx = 0; idx < shape->points.size(); idx++) {
      auto& p             = shape->points[idx];
      auto& primitive     = primitives.emplace_back();
      primitive.bbox      = point_bounds(shape->positions[p], shape->radius[p]);
      primitive.center    = center(primitive.bbox);
      primitive.primitive = idx;
    }
  } else if (!shape->lines.empty()) {
    for (auto idx = 0; idx < shape->lines.size(); idx++) {
      auto& l         = shape->lines[idx];
      auto& primitive = primitives.emplace_back();
      primitive.bbox = line_bounds(shape->positions[l.x], shape->positions[l.y],
          shape->radius[l.x], shape->radius[l.y]);
      primitive.center    = center(primitive.bbox);
      primitive.primitive = idx;
    }
  } else if (!shape->triangles.empty()) {
    for (auto idx = 0; idx < shape->triangles.size(); idx++) {
      auto& primitive = primitives.emplace_back();
      auto& t         = shape->triangles[idx];
      primitive.bbox  = triangle_bounds(
          shape->positions[t.x], shape->positions[t.y], shape->positions[t.z]);
      primitive.center    = center(primitive.bbox);
      primitive.primitive = idx;
    }
  }

  // build nodes
  if (shape->bvh) delete shape->bvh;
  shape->bvh = new raytrace_bvh_tree{};
  build_bvh(shape->bvh->nodes, primitives);

  // set bvh primitives
  shape->bvh->primitives.reserve(primitives.size());
  for (auto& primitive : primitives) {
    shape->bvh->primitives.push_back(primitive.primitive);
  }
}

void init_bvh(raytrace_scene* scene, const raytrace_params& params,
    progress_callback progress_cb) {
  // handle progress
  auto progress = vec2i{0, 1 + (int)scene->shapes.size()};

  // shapes
  for (auto idx = 0; idx < scene->shapes.size(); idx++) {
    if (progress_cb) progress_cb("build shape bvh", progress.x++, progress.y);
    init_bvh(scene->shapes[idx], params);
  }

  // handle progress
  if (progress_cb) progress_cb("build scene bvh", progress.x++, progress.y);

  // instance bboxes
  auto primitives = vector<raytrace_bvh_primitive>{};
  auto object_id  = 0;
  for (auto instance : scene->instances) {
    auto& primitive = primitives.emplace_back();
    primitive.bbox  = instance->shape->bvh->nodes.empty()
                         ? invalidb3f
                         : transform_bbox(instance->frame,
                               instance->shape->bvh->nodes[0].bbox);
    primitive.center    = center(primitive.bbox);
    primitive.primitive = object_id++;
  }

  // build nodes
  if (scene->bvh) delete scene->bvh;
  scene->bvh = new raytrace_bvh_tree{};
  build_bvh(scene->bvh->nodes, primitives);

  // set bvh primitives
  scene->bvh->primitives.reserve(primitives.size());
  for (auto& primitive : primitives) {
    scene->bvh->primitives.push_back(primitive.primitive);
  }

  // handle progress
  if (progress_cb) progress_cb("build bvh", progress.x++, progress.y);
}

// Intersect ray with a bvh->
static bool intersect_shape_bvh(raytrace_shape* shape, const ray3f& ray_,
    int& element, vec2f& uv, float& distance, bool find_any) {
  // get bvh and shape pointers for fast access
  auto bvh = shape->bvh;

  // check empty
  if (bvh->nodes.empty()) return false;

  // node stack
  int  node_stack[128];
  auto node_cur          = 0;
  node_stack[node_cur++] = 0;

  // shared variables
  auto hit = false;

  // copy ray to modify it
  auto ray = ray_;

  // prepare ray for fast queries
  auto ray_dinv  = vec3f{1 / ray.d.x, 1 / ray.d.y, 1 / ray.d.z};
  auto ray_dsign = vec3i{(ray_dinv.x < 0) ? 1 : 0, (ray_dinv.y < 0) ? 1 : 0,
      (ray_dinv.z < 0) ? 1 : 0};

  // walking stack
  while (node_cur) {
    // grab node
    auto& node = bvh->nodes[node_stack[--node_cur]];

    // intersect bbox
    // if (!intersect_bbox(ray, ray_dinv, ray_dsign, node.bbox)) continue;
    if (!intersect_bbox(ray, ray_dinv, node.bbox)) continue;

    // intersect node, switching based on node type
    // for each type, iterate over the the primitive list
    if (node.internal) {
      // for internal nodes, attempts to proceed along the
      // split axis from smallest to largest nodes
      if (ray_dsign[node.axis]) {
        node_stack[node_cur++] = node.start + 0;
        node_stack[node_cur++] = node.start + 1;
      } else {
        node_stack[node_cur++] = node.start + 1;
        node_stack[node_cur++] = node.start + 0;
      }
    } else if (!shape->points.empty()) {
      for (auto idx = node.start; idx < node.start + node.num; idx++) {
        auto& p = shape->points[shape->bvh->primitives[idx]];
        if (intersect_point(
                ray, shape->positions[p], shape->radius[p], uv, distance)) {
          hit      = true;
          element  = shape->bvh->primitives[idx];
          ray.tmax = distance;
        }
      }
    } else if (!shape->lines.empty()) {
      for (auto idx = node.start; idx < node.start + node.num; idx++) {
        auto& l = shape->lines[shape->bvh->primitives[idx]];
        if (intersect_line(ray, shape->positions[l.x], shape->positions[l.y],
                shape->radius[l.x], shape->radius[l.y], uv, distance)) {
          hit      = true;
          element  = shape->bvh->primitives[idx];
          ray.tmax = distance;
        }
      }
    } else if (!shape->triangles.empty()) {
      for (auto idx = node.start; idx < node.start + node.num; idx++) {
        auto& t = shape->triangles[shape->bvh->primitives[idx]];
        if (intersect_triangle(ray, shape->positions[t.x],
                shape->positions[t.y], shape->positions[t.z], uv, distance)) {
          hit      = true;
          element  = shape->bvh->primitives[idx];
          ray.tmax = distance;
        }
      }
    }

    // check for early exit
    if (find_any && hit) return hit;
  }

  return hit;
}

// Intersect ray with a bvh->
static bool intersect_scene_bvh(const raytrace_scene* scene, const ray3f& ray_,
    int& instance, int& element, vec2f& uv, float& distance, bool find_any,
    bool non_rigid_frames) {
  // get bvh and scene pointers for fast access
  auto bvh = scene->bvh;

  // check empty
  if (bvh->nodes.empty()) return false;

  // node stack
  int  node_stack[128];
  auto node_cur          = 0;
  node_stack[node_cur++] = 0;

  // shared variables
  auto hit = false;

  // copy ray to modify it
  auto ray = ray_;

  // prepare ray for fast queries
  auto ray_dinv  = vec3f{1 / ray.d.x, 1 / ray.d.y, 1 / ray.d.z};
  auto ray_dsign = vec3i{(ray_dinv.x < 0) ? 1 : 0, (ray_dinv.y < 0) ? 1 : 0,
      (ray_dinv.z < 0) ? 1 : 0};

  // walking stack
  while (node_cur) {
    // grab node
    auto& node = bvh->nodes[node_stack[--node_cur]];

    // intersect bbox
    // if (!intersect_bbox(ray, ray_dinv, ray_dsign, node.bbox)) continue;
    if (!intersect_bbox(ray, ray_dinv, node.bbox)) continue;

    // intersect node, switching based on node type
    // for each type, iterate over the the primitive list
    if (node.internal) {
      // for internal nodes, attempts to proceed along the
      // split axis from smallest to largest nodes
      if (ray_dsign[node.axis]) {
        node_stack[node_cur++] = node.start + 0;
        node_stack[node_cur++] = node.start + 1;
      } else {
        node_stack[node_cur++] = node.start + 1;
        node_stack[node_cur++] = node.start + 0;
      }
    } else {
      for (auto idx = node.start; idx < node.start + node.num; idx++) {
        auto instance_ = scene->instances[scene->bvh->primitives[idx]];
        auto inv_ray   = transform_ray(
            inverse(instance_->frame, non_rigid_frames), ray);
        if (intersect_shape_bvh(
                instance_->shape, inv_ray, element, uv, distance, find_any)) {
          hit      = true;
          instance = scene->bvh->primitives[idx];
          ray.tmax = distance;
        }
      }
    }

    // check for early exit
    if (find_any && hit) return hit;
  }

  return hit;
}

// Intersect ray with a bvh->
static bool intersect_instance_bvh(const raytrace_instance* instance,
    const ray3f& ray, int& element, vec2f& uv, float& distance, bool find_any,
    bool non_rigid_frames) {
  auto inv_ray = transform_ray(inverse(instance->frame, non_rigid_frames), ray);
  return intersect_shape_bvh(
      instance->shape, inv_ray, element, uv, distance, find_any);
}

raytrace_intersection intersect_scene_bvh(const raytrace_scene* scene,
    const ray3f& ray, bool find_any, bool non_rigid_frames) {
  auto intersection = raytrace_intersection{};
  intersection.hit  = intersect_scene_bvh(scene, ray, intersection.instance,
      intersection.element, intersection.uv, intersection.distance, find_any,
      non_rigid_frames);
  return intersection;
}
raytrace_intersection intersect_instance_bvh(const raytrace_instance* instance,
    const ray3f& ray, bool find_any, bool non_rigid_frames) {
  auto intersection = raytrace_intersection{};
  intersection.hit = intersect_instance_bvh(instance, ray, intersection.element,
      intersection.uv, intersection.distance, find_any, non_rigid_frames);
  return intersection;
}

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMPLEMENTATION FOR PATH TRACING
// -----------------------------------------------------------------------------
namespace yocto {

// THIS SHADER IS ACTUALLY____NOT_____ IMPLEMENTED
static vec4f shade_hologram(const raytrace_scene* scene, const ray3f& ray,
    int bounce, rng_state& rng, const raytrace_params& params) {
  // YOUR CODE GOES HERE -----------------------

  return zero4f;
}

// BONUS:
static vec4f shade_cartoon(const raytrace_scene* scene, const ray3f& ray,
    int bounce, rng_state& rng, const raytrace_params& params) {
  // YOUR CODE GOES HERE -----------------------
  auto isec = intersect_scene_bvh(scene, ray);
  if (!isec.hit) {
    auto ee = eval_environment(scene, ray);
    return vec4f{ee.x, ee.y, ee.z, 1};
  }

  // Geometry and other stuff
  auto instance = scene->instances[isec.instance];
  auto shape    = instance->shape;
  auto material = instance->material;
  auto position = transform_point(
      instance->frame, eval_position(shape, isec.element, isec.uv));
  auto normal = transform_direction(
      instance->frame, eval_normal(shape, isec.element, isec.uv));
  auto outgoing = -ray.d;

  // Materials and textures

  auto texcoord = eval_texcoord(shape, isec.element, isec.uv);

  auto color = material->color *
               xyz(eval_texture(material->color_tex, texcoord));
  auto radiance = color;

  // I got issue over normals!
  if (!instance->shape->lines.empty()) {
    // When we have line we don't wanna parallel line, but tangent ones.
    normal = orthonormalize(outgoing, normal);
  } else if (!instance->shape->triangles.empty()) {
    // if the normal is not looking outward we should flip it
    if (dot(outgoing, normal) < 0) {
      normal = -normal;
    }
  }

  // Incoming direction. We take it as
  // ________________________FIXED______________________________
  auto incoming = vec3f{10, 10, 10};

  // At the top of the fragment shader
  float NdotL = dot(incoming, normal);

  // auto lux = NdotL > 0 ? 1 : 0;
  auto isec2 = intersect_scene_bvh(scene, {position, incoming});
  auto lux   = isec2.hit ? 0 : 1;

  // We divide the lighting into: light and dark. The idea is to pass smoothly
  // from light to dark and is achieved through the smoothstep function. It is
  // non-lonear: it returns a value between 0 and 1 based on how far this
  // third value is between the bounds

  auto light_intensity = smoothstep(0, 0.01, NdotL);
  auto light           = light_intensity * lux;

  //_____________________________AMBIENT_________________________________
  // This represents light that bounces off the surfacs of objects in the area
  // and is scattered in the atmoshpere
  auto ambient_color = vec4f{0.4, 0.4, 0.4, 1.f};

  //_________________________ SPECULAR __________________________________
  // reflection models the individual, distinct reflections made by
  // light source.
  auto specular_color = vec4f{0.9, 0.9, 0.9, 1.f};
  // Lucentezza
  auto glossiness = 32;

  // Why halfway ? The strenght of the specular reflection is defined in
  // Blinn-PHong as the dor between the normal and the half vectors.
  auto halfway = normalize(incoming + outgoing);
  auto NdotH   = dot(normal, halfway);

  auto specular_intensity = pow(
      NdotH * light_intensity, glossiness * glossiness);

  auto specular = smoothstep(0.005, 0.01, specular_intensity) * specular_color;

  //____________________________RIM LIGHTING_____________________________

  // It is the addition of illumination to the edges of an object to simulate
  // the reflect light or backlighiting. Useful for the silhouette standing
  // out among the flat shaded surfaces

  auto rim_dot       = 1 - dot(normal, outgoing);
  auto rim_color     = vec4f{1.f, 1.f, 1.f, 1.f};
  auto rim_amount    = 0.716;
  auto rim_threshold = 0.1;

  // We'd like to make it looks like a light more than a line, thus we
  // multiply the rimdot for the NdotL
  auto rim_intensity = rim_dot * pow(NdotL, rim_threshold);
  rim_intensity      = smoothstep(
      rim_amount - 0.01, rim_amount + 0.01, rim_intensity);
  auto rim = rim_intensity * rim_color;

  radiance *= (light + xyz(ambient_color) + xyz(specular) + xyz(rim));

  return rgb_to_rgba(radiance);
}  // namespace yocto

// _____________________________________________________________________________

// Raytrace renderer.
static vec4f shade_raytrace(const raytrace_scene* scene, const ray3f& ray,
    int bounce, rng_state& rng, const raytrace_params& params) {
  // YOUR CODE GOES HERE -----------------------
  auto isec = intersect_scene_bvh(scene, ray);
  if (!isec.hit) {
    auto ee = eval_environment(scene, ray);
    return vec4f{ee.x, ee.y, ee.z, 1};
    // return eval_environment;
  }

  // Geometry and other stuff
  auto instance = scene->instances[isec.instance];
  auto shape    = instance->shape;
  auto material = instance->material;
  auto position = transform_point(
      instance->frame, eval_position(shape, isec.element, isec.uv));
  auto normal = transform_direction(
      instance->frame, eval_normal(shape, isec.element, isec.uv));
  auto outgoing      = -ray.d;
  auto dp_refraction = dot(outgoing, normal);

  // I got issue over normals!
  if (!instance->shape->lines.empty()) {
    // When we have line we don't wanna parallel line, but tangent ones.
    normal = orthonormalize(outgoing, normal);
  } else if (!instance->shape->triangles.empty()) {
    // if the normal is not looking outward we should flip it
    if (dot(outgoing, normal) < 0) {
      normal = -normal;
    }
  }

  // Materials and textures
  auto texcoord = eval_texcoord(shape, isec.element, isec.uv);
  auto color    = material->color *
               xyz(eval_texture(material->color_tex, texcoord));
  auto emission = material->emission *
                  xyz(eval_texture(material->emission_tex, texcoord));
  auto specular = material->specular *
                  xyz(eval_texture(material->specular_tex, texcoord)).x;
  auto metallic = material->metallic *
                  (eval_texture(material->metallic_tex, texcoord)).x;
  auto roughness = material->roughness *
                   (eval_texture(material->roughness_tex, texcoord)).x;

  // That's a convetion
  roughness = pow(roughness, 2);

  auto transmission = material->transmission *
                      (eval_texture(material->transmission_tex, texcoord)).x;

  // Handling opacity
  auto opacity = material->opacity;
  if (material->opacity_tex)
    opacity = opacity * (eval_texture(material->opacity_tex, texcoord)).x;

  // We setup the color
  auto radiance = emission;

  // Exit if enough bounces are done
  if (bounce >= params.bounces) {
    return {radiance.x, radiance.y, radiance.z, 1};
  }

  // Check on opacity
  if (rand1f(rng) > opacity) {
    auto incoming = -outgoing;
    return shade_raytrace(scene, {position, incoming}, bounce + 1, rng, params);
  }

  /*
    ___________________________________________________________
    _                                                         _
    _                                                         _
    _                     HANDLING MATERIALS                  _
    _                                                         _
    _                                                         _
    ___________________________________________________________
  */

  if (transmission) {
    //__________________Polished dieletric__________________

    // Slide 115:
    // Scatter light both REFLECTING and TRANSMITTING

    // The transmitted light is refracted into a material.  Since we wanto to
    // have ONLY ONE RAY CONTINUE, we randomly pick which direction to go
    // based on the fresnel term ---> not update Fresnel weights. The
    // direction to go The reflection coefficient is very small:
    //                        K_s = 0.04

    // Evaluate Fresnel term:
    // Scatter light like mirrors (reflect light therefore). The color of
    // surface changes at normal incidence following the FRESNEL laws.
    auto fs = fresnel_schlick(vec3f{0.04, 0.04, 0.04}, normal, outgoing);

    if (material->thin) {
      if (rand1f(rng) < mean(fs)) {
        auto incoming = reflect(outgoing, normal);
        radiance += xyz(shade_raytrace(
            scene, {position, incoming}, bounce + 1, rng, params));
      } else {
        auto incoming = -outgoing;
        radiance += color * xyz(shade_raytrace(scene, {position, incoming},
                                bounce + 1, rng, params));
      }
    } else {
      // Ior of the material in which is entering
      auto ior = reflectivity_to_eta(vec3f{0.04, 0.04, 0.04});

      // Eta as the ratio of the two ior (or eta_i and eta_j). Ior = 1.f is the
      // air ior
      auto eta = 1.f / mean(ior);

      // Randomly? Instead of weighting both reflect and transmission, we just
      // use randomization
      if (rand1f(rng) < mean(fs)) {
        auto incoming = reflect(outgoing, normal);
        radiance += xyz(shade_raytrace(
            scene, {position, incoming}, bounce + 1, rng, params));
      } else {
        // When the ray leaves the surface we gotta invert eta
        auto incoming = dp_refraction < 0 ? refract(outgoing, normal, 1 / eta)
                                          : refract(outgoing, normal, eta);

        radiance += color * xyz(shade_raytrace(scene, {position, incoming},
                                bounce + 1, rng, params));
      }
    }
  } else if (metallic && !roughness) {
    //__________________Polisehd metal__________________

    // Slide 102
    // They scatter light like mirrors (reflect light therefore). The color of
    // surface changes at normal incidence following the FRESNEL laws.

    auto incoming = reflect(outgoing, normal);
    auto fs       = fresnel_schlick(color, normal, outgoing);
    radiance += fs * xyz(shade_raytrace(
                         scene, {position, incoming}, bounce + 1, rng, params));
  } else if (metallic && roughness) {
    //__________________Rough metal__________________

    // slide 105/106
    // Why bisector (or halway) and not normal? That's because the effctive
    // normal of the microfacet is the halfway h of the incoming and outgoing,
    // namely:
    //                          o+i
    //                     h = _____
    //
    //                         |o+i|

    // Incoming direction
    auto incoming = sample_hemisphere(normal, rand2f(rng));

    // Bisector
    auto halfway = normalize(outgoing + incoming);

    // Fresnel term
    auto fs = fresnel_schlick(color, halfway, outgoing);

    // Each micro facet scatters light like a metallic mirror. Thus, the
    // distribution represent the ratio of microfacets oriented along the
    // bisector h
    auto distribution = microfacet_distribution(roughness, normal, halfway);

    // The shadowing term captures the ratio of microfacet that is visible
    // from a particular incoming and outgoing angles
    auto geometric = microfacet_shadowing(
        roughness, normal, halfway, outgoing, incoming);

    // Denominator of each summation term:
    //    4*|normal * outgoing|*|normal * incoming|
    auto denominator = 4 * abs(dot(normal, incoming)) *
                       abs(dot(normal, outgoing));
    // Each summation term
    auto sum_term = fs * distribution * geometric / denominator;

    // Computing finally the lighting (recursively)
    radiance += (2 * pif) * abs(dot(normal, incoming)) * sum_term *
                xyz(shade_raytrace(
                    scene, {position, incoming}, bounce + 1, rng, params));
  } else if (specular) {
    //__________________Plastic__________________

    // Slide 111
    // Plastic is modelled as a matte surfaced coated with a thing dieletric
    // layer. That's approximated as sum of a diffuse and a specular
    // contribution:
    //  - the SPECULAR LAYER REFLECTS the light (K_s = 0.04 is good)
    //  - the DIFFUSE COMPONENT is weighted by one minus the contribution of
    //  the
    //    specular one to conserve energy

    // Incoming direction
    auto incoming = sample_hemisphere(normal, rand2f(rng));

    // Halfway
    auto halfway = normalize(outgoing + incoming);

    // Fresnel term
    auto fs = fresnel_schlick(vec3f{0.04, 0.04, 0.04}, halfway, outgoing);

    // Each micro facet scatters light like a metallic mirror. Thus, the
    // distribution represent the ratio of microfacets oriented along the
    // bisector h
    auto distribution = microfacet_distribution(roughness, normal, halfway);

    // The shadowing term captures the ratio of microfacet that is visible
    // from a particular incoming and outgoing angles
    auto geometric = microfacet_shadowing(
        roughness, normal, halfway, outgoing, incoming);

    // Denominator of each summation term:
    //    4*|normal * outgoing|*|normal * incoming|
    auto denominator = 4 * abs(dot(normal, incoming)) *
                       abs(dot(normal, outgoing));

    // Each summation term
    auto sum_term = fs * distribution * geometric / denominator;

    // Computing finally the lighting (recursively)
    radiance += (2 * pif) * (color / pif * (1 - fs) + sum_term) *
                abs(dot(normal, incoming)) *
                xyz(shade_raytrace(
                    scene, {position, incoming}, bounce + 1, rng, params));
  } else {
    //__________________Diffuse light__________________
    auto incoming = sample_hemisphere(normal, rand2f(rng));

    // Shadow rounds errors: we add a small epsilon whether the ray intersect
    // the sphape ah the shading point (already done when creating a ray)
    radiance += (2 * pif) * (color / pif) * abs(dot(normal, incoming)) *
                xyz(shade_raytrace(
                    scene, {position, incoming}, bounce + 1, rng, params));
  }

  // auto dp = color * abs(dot(normal, incoming));
  return rgb_to_rgba(radiance);
}  // namespace yocto

// Eyelight for quick previewing.
static vec4f shade_eyelight(const raytrace_scene* scene, const ray3f& ray,
    int bounce, rng_state& rng, const raytrace_params& params) {
  // YOUR CODE GOES HERE -----------------------
  auto isec = intersect_scene_bvh(scene, ray);
  if (!isec.hit) {
    return vec4f{0, 0, 0, 1.0f};
  }
  auto instance = scene->instances[isec.instance];
  auto normal   = transform_direction(
      instance->frame, eval_normal(instance->shape, isec.element, isec.uv));

  auto dp = instance->material->color * dot(normal, -ray.d);

  return rgb_to_rgba(dp);

}  // namespace yocto

static vec4f shade_normal(const raytrace_scene* scene, const ray3f& ray,
    int bounce, rng_state& rng, const raytrace_params& params) {
  // YOUR CODE GOES HERE -----------------------
  auto isec = intersect_scene_bvh(scene, ray);
  if (!isec.hit) return vec4f{0, 0, 0, 1};

  auto instance = scene->instances[isec.instance];
  auto shape    = instance->shape;
  auto normal   = transform_direction(
      instance->frame, eval_normal(shape, isec.element, isec.uv));

  return rgb_to_rgba(normal * 0.5f + 0.5f);
}

static vec4f shade_texcoord(const raytrace_scene* scene, const ray3f& ray,
    int bounce, rng_state& rng, const raytrace_params& params) {
  // YOUR CODE GOES HERE -----------------------
  auto isec = intersect_scene_bvh(scene, ray);
  if (!isec.hit) return vec4f{0, 0, 0, 1};

  auto instance = scene->instances[isec.instance];
  auto texcoord = eval_texcoord(instance->shape, isec.element, isec.uv);

  auto vx = fmod(texcoord.x, 1);
  auto vy = fmod(texcoord.y, 1);

  return vec4f{vx, vy, 0, 1};
}

static vec4f shade_color(const raytrace_scene* scene, const ray3f& ray,
    int bounce, rng_state& rng, const raytrace_params& params) {
  // YOUR CODE GOES HERE -----------------------
  auto isec = intersect_scene_bvh(scene, ray);
  if (!isec.hit) return vec4f{0, 0, 0, 1};

  auto instance = scene->instances[isec.instance];
  return rgb_to_rgba(instance->material->color);
}

// Trace a single ray from the camera using the given algorithm.
using raytrace_shader_func = vec4f (*)(const raytrace_scene* scene,
    const ray3f& ray, int bounce, rng_state& rng,
    const raytrace_params& params);
static raytrace_shader_func get_shader(const raytrace_params& params) {
  switch (params.shader) {
    case raytrace_shader_type::raytrace: return shade_raytrace;
    case raytrace_shader_type::cartoon: return shade_cartoon;
    case raytrace_shader_type::eyelight: return shade_eyelight;
    case raytrace_shader_type::normal: return shade_normal;
    case raytrace_shader_type::texcoord: return shade_texcoord;
    case raytrace_shader_type::color: return shade_color;
    case raytrace_shader_type::hologram: return shade_hologram;

    default: {
      throw std::runtime_error("sampler unknown");
      return nullptr;
    }
  }
}

// Trace a block of samples
void render_sample(raytrace_state* state, const raytrace_scene* scene,
    const raytrace_camera* camera, const vec2i& ij,
    const raytrace_params& params) {
  // YOUR CODE GOES HERE -----------------------
  // init_state(state, scene, camera, params);
  return render_samples(state, scene, camera, params);
}

// Init a sequence of random number generators.
void init_state(raytrace_state* state, const raytrace_scene* scene,
    const raytrace_camera* camera, const raytrace_params& params) {
  auto image_size =
      (camera->film.x > camera->film.y)
          ? vec2i{params.resolution,
                (int)round(params.resolution * camera->film.y / camera->film.x)}
          : vec2i{
                (int)round(params.resolution * camera->film.x / camera->film.y),
                params.resolution};
  state->render.assign(image_size, zero4f);
  state->accumulation.assign(image_size, zero4f);
  state->samples.assign(image_size, 0);
  state->rngs.assign(image_size, {});
  auto init_rng = make_rng(1301081);
  for (auto& rng : state->rngs) {
    rng = make_rng(params.seed, rand1i(init_rng, 1 << 31) / 2 + 1);
  }
}

// Progressively compute an image by calling trace_samples multiple times.
void render_samples(raytrace_state* state, const raytrace_scene* scene,
    const raytrace_camera* camera, const raytrace_params& params) {
  // YOUR CODE GOES HERE -----------------------
  auto shader = get_shader(params);
  if (params.noparallel) {
    auto sy = state->render.imsize().y;
    auto sx = state->render.imsize().x;

    for (auto j = 0; j < sy; j++) {
      for (auto i = 0; i < sx; i++) {
        // we get uv pixel from rng
        auto puv = rand2f(state->rngs[{i, j}]);
        auto d   = vec2f{(float)i, (float)j} + puv;

        auto uv = vec2f{d.x / sx, d.y / sy};

        // we get the camera ray
        auto ray = eval_camera(camera, uv);

        // We get the proper shader
        auto color = shader(scene, ray, 0, state->rngs[i, j], params);

        // Here we clamp to max value
        if (length(xyz(color)) > params.clamp)
          color = normalize(color) * params.clamp;

        // update state accumulation, samples and render
        state->accumulation[{i, j}] += color;
        state->samples[{i, j}] += 1;
        state->render[{i, j}] = (state->accumulation[{i, j}]) /
                                (state->samples[{i, j}]);
      };
    }
  } else {
    // YOUR CODE GOES HERE -----------------------

    parallel_for(state->render.imsize().x, state->render.imsize().y,
        [state, scene, camera, shader, &params](int i, int j) {
          // Image dimension
          // auto sy = state->render.imsize().y;
          // auto sx = state->render.imsize().x;
          auto sy = state->render.imsize().y;
          auto sx = state->render.imsize().x;

          auto ij_v = vec2i{i, j};

          // puv (ranodm between [0-1) )
          auto puv = rand2f(state->rngs[ij_v]);

          // d: pixel coordinate
          auto d = vec2f{(float)i, (float)j} + puv;

          // uv-pix
          auto uv = vec2f{d.x / sx, d.y / sy};

          // we get the camera ray
          auto ray = eval_camera(camera, uv);

          // We get the proper shader
          auto color = shader(scene, ray, 0, state->rngs[i, j], params);

          // Here we clamp to max value
          if (length(xyz(color)) > params.clamp) {
            color = normalize(color) * params.clamp;
          }

          // update state accumulation, samples and render

          state->accumulation[ij_v] += color;
          state->samples[ij_v] += 1;
          state->render[ij_v] = (state->accumulation[ij_v]) /
                                (state->samples[ij_v]);
        });
  }

}  // namespace yocto
}  // namespace yocto

// -----------------------------------------------------------------------------
// SCENE CREATION
// -----------------------------------------------------------------------------
namespace yocto {

// cleanup
raytrace_shape::~raytrace_shape() {
  if (bvh) delete bvh;
}

// cleanup
raytrace_scene::~raytrace_scene() {
  if (bvh) delete bvh;
  for (auto camera : cameras) delete camera;
  for (auto instance : instances) delete instance;
  for (auto shape : shapes) delete shape;
  for (auto material : materials) delete material;
  for (auto texture : textures) delete texture;
  for (auto environment : environments) delete environment;
}

// Add element
raytrace_camera* add_camera(raytrace_scene* scene) {
  return scene->cameras.emplace_back(new raytrace_camera{});
}
raytrace_texture* add_texture(raytrace_scene* scene) {
  return scene->textures.emplace_back(new raytrace_texture{});
}
raytrace_shape* add_shape(raytrace_scene* scene) {
  return scene->shapes.emplace_back(new raytrace_shape{});
}
raytrace_material* add_material(raytrace_scene* scene) {
  return scene->materials.emplace_back(new raytrace_material{});
}
raytrace_instance* add_instance(raytrace_scene* scene) {
  return scene->instances.emplace_back(new raytrace_instance{});
}
raytrace_environment* add_environment(raytrace_scene* scene) {
  return scene->environments.emplace_back(new raytrace_environment{});
}

// Set cameras
void set_frame(raytrace_camera* camera, const frame3f& frame) {
  camera->frame = frame;
}
void set_lens(raytrace_camera* camera, float lens, float aspect, float film) {
  camera->lens = lens;
  camera->film = aspect >= 1 ? vec2f{film, film / aspect}
                             : vec2f{film * aspect, film};
}
void set_focus(raytrace_camera* camera, float aperture, float focus) {
  camera->aperture = aperture;
  camera->focus    = focus;
}

// Add texture
void set_texture(raytrace_texture* texture, const image<vec4b>& img) {
  texture->ldr = img;
  texture->hdr = {};
}
void set_texture(raytrace_texture* texture, const image<vec4f>& img) {
  texture->ldr = {};
  texture->hdr = img;
}

// Add shape
void set_points(raytrace_shape* shape, const vector<int>& points) {
  shape->points = points;
}
void set_lines(raytrace_shape* shape, const vector<vec2i>& lines) {
  shape->lines = lines;
}
void set_triangles(raytrace_shape* shape, const vector<vec3i>& triangles) {
  shape->triangles = triangles;
}
void set_positions(raytrace_shape* shape, const vector<vec3f>& positions) {
  shape->positions = positions;
}
void set_normals(raytrace_shape* shape, const vector<vec3f>& normals) {
  shape->normals = normals;
}
void set_texcoords(raytrace_shape* shape, const vector<vec2f>& texcoords) {
  shape->texcoords = texcoords;
}
void set_radius(raytrace_shape* shape, const vector<float>& radius) {
  shape->radius = radius;
}

// Add instance
void set_frame(raytrace_instance* instance, const frame3f& frame) {
  instance->frame = frame;
}
void set_shape(raytrace_instance* instance, raytrace_shape* shape) {
  instance->shape = shape;
}
void set_material(raytrace_instance* instance, raytrace_material* material) {
  instance->material = material;
}

// Add material
void set_emission(raytrace_material* material, const vec3f& emission,
    raytrace_texture* emission_tex) {
  material->emission     = emission;
  material->emission_tex = emission_tex;
}
void set_color(raytrace_material* material, const vec3f& color,
    raytrace_texture* color_tex) {
  material->color     = color;
  material->color_tex = color_tex;
}
void set_specular(raytrace_material* material, float specular,
    raytrace_texture* specular_tex) {
  material->specular     = specular;
  material->specular_tex = specular_tex;
}
void set_metallic(raytrace_material* material, float metallic,
    raytrace_texture* metallic_tex) {
  material->metallic     = metallic;
  material->metallic_tex = metallic_tex;
}
void set_ior(raytrace_material* material, float ior) { material->ior = ior; }
void set_transmission(raytrace_material* material, float transmission,
    bool thin, float trdepth, raytrace_texture* transmission_tex) {
  material->transmission     = transmission;
  material->thin             = thin;
  material->trdepth          = trdepth;
  material->transmission_tex = transmission_tex;
}
void set_thin(raytrace_material* material, bool thin) { material->thin = thin; }
void set_roughness(raytrace_material* material, float roughness,
    raytrace_texture* roughness_tex) {
  material->roughness     = roughness;
  material->roughness_tex = roughness_tex;
}
void set_opacity(
    raytrace_material* material, float opacity, raytrace_texture* opacity_tex) {
  material->opacity     = opacity;
  material->opacity_tex = opacity_tex;
}
void set_scattering(raytrace_material* material, const vec3f& scattering,
    float scanisotropy, raytrace_texture* scattering_tex) {
  material->scattering     = scattering;
  material->scanisotropy   = scanisotropy;
  material->scattering_tex = scattering_tex;
}

// Add environment
void set_frame(raytrace_environment* environment, const frame3f& frame) {
  environment->frame = frame;
}
void set_emission(raytrace_environment* environment, const vec3f& emission,
    raytrace_texture* emission_tex) {
  environment->emission     = emission;
  environment->emission_tex = emission_tex;
}

}  // namespace yocto

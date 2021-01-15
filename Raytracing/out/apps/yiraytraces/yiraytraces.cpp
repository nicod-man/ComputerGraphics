//
// LICENSE:
//
// Copyright (c) 2016 -- 2020 Fabio Pellacini
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//

#include <yocto/yocto_color.h>
#include <yocto/yocto_commonio.h>
#include <yocto/yocto_sceneio.h>
#include <yocto/yocto_shape.h>
#include <yocto_gui/yocto_imgui.h>
#include <yocto_gui/yocto_opengl.h>
#include <yocto_raytrace/yocto_raytrace.h>
using namespace yocto;

#include <memory>

// Application state
struct app_state {
  // loading options
  string filename  = "scene.yaml";
  string imagename = "out.png";
  string name      = "";

  // options
  raytrace_params params = {};

  // scene
  raytrace_scene*  scene  = new raytrace_scene{};
  raytrace_camera* camera = nullptr;

  // rendering state
  image<vec4f> render   = {};
  image<vec4f> display  = {};
  float        exposure = 0;

  // view scene
  ogl_image*       glimage  = new ogl_image{};
  ogl_image_params glparams = {};

  // computation
  int             render_sample  = 0;
  int             render_counter = 0;
  raytrace_state* render_state   = new raytrace_state{};
  future<void>    render_worker  = {};
  atomic<bool>    render_stop    = {};

  ~app_state() {
    if (render_worker.valid()) {
      render_stop = true;
      render_worker.get();
    }
    if (render_state) delete render_state;
    if (scene) delete scene;
    if (glimage) delete glimage;
  }
};

// construct a scene from io
void init_scene(raytrace_scene* scene, sceneio_scene* ioscene,
    raytrace_camera*& camera, sceneio_camera* iocamera,
    progress_callback print_progress = {}) {
  // handle progress
  auto progress = vec2i{
      0, (int)ioscene->cameras.size() + (int)ioscene->environments.size() +
             (int)ioscene->materials.size() + (int)ioscene->textures.size() +
             (int)ioscene->shapes.size() + (int)ioscene->instances.size()};

  auto camera_map     = unordered_map<sceneio_camera*, raytrace_camera*>{};
  camera_map[nullptr] = nullptr;
  for (auto iocamera : ioscene->cameras) {
    if (print_progress)
      print_progress("convert camera", progress.x++, progress.y);
    auto camera = add_camera(scene);
    set_frame(camera, iocamera->frame);
    set_lens(camera, iocamera->lens, iocamera->aspect, iocamera->film);
    set_focus(camera, iocamera->aperture, iocamera->focus);
    camera_map[iocamera] = camera;
  }

  auto texture_map     = unordered_map<sceneio_texture*, raytrace_texture*>{};
  texture_map[nullptr] = nullptr;
  for (auto iotexture : ioscene->textures) {
    if (print_progress)
      print_progress("convert texture", progress.x++, progress.y);
    auto texture = add_texture(scene);
    if (!iotexture->hdr.empty()) {
      set_texture(texture, iotexture->hdr);
    } else if (!iotexture->ldr.empty()) {
      set_texture(texture, iotexture->ldr);
    }
    texture_map[iotexture] = texture;
  }

  auto material_map = unordered_map<sceneio_material*, raytrace_material*>{};
  material_map[nullptr] = nullptr;
  for (auto iomaterial : ioscene->materials) {
    if (print_progress)
      print_progress("convert material", progress.x++, progress.y);
    auto material = add_material(scene);
    set_emission(material, iomaterial->emission,
        texture_map.at(iomaterial->emission_tex));
    set_color(
        material, iomaterial->color, texture_map.at(iomaterial->color_tex));
    set_specular(material, iomaterial->specular,
        texture_map.at(iomaterial->specular_tex));
    set_ior(material, iomaterial->ior);
    set_metallic(material, iomaterial->metallic,
        texture_map.at(iomaterial->metallic_tex));
    set_transmission(material, iomaterial->transmission, iomaterial->thin,
        iomaterial->trdepth, texture_map.at(iomaterial->transmission_tex));
    set_roughness(material, iomaterial->roughness,
        texture_map.at(iomaterial->roughness_tex));
    set_opacity(
        material, iomaterial->opacity, texture_map.at(iomaterial->opacity_tex));
    set_thin(material, iomaterial->thin);
    set_scattering(material, iomaterial->scattering, iomaterial->scanisotropy,
        texture_map.at(iomaterial->scattering_tex));
    material_map[iomaterial] = material;
  }

  auto shape_map     = unordered_map<sceneio_shape*, raytrace_shape*>{};
  shape_map[nullptr] = nullptr;
  for (auto ioshape : ioscene->shapes) {
    if (print_progress)
      print_progress("convert shape", progress.x++, progress.y);
    auto shape = add_shape(scene);
    set_points(shape, ioshape->points);
    set_lines(shape, ioshape->lines);
    set_triangles(shape, ioshape->triangles);
    if (!ioshape->quads.empty())
      set_triangles(shape, quads_to_triangles(ioshape->quads));
    set_positions(shape, ioshape->positions);
    set_normals(shape, ioshape->normals);
    set_texcoords(shape, ioshape->texcoords);
    set_radius(shape, ioshape->radius);
    shape_map[ioshape] = shape;
  }

  for (auto ioinstance : ioscene->instances) {
    if (print_progress)
      print_progress("convert object", progress.x++, progress.y);
    auto instance = add_instance(scene);
    set_frame(instance, ioinstance->frame);
    set_shape(instance, shape_map.at(ioinstance->shape));
    set_material(instance, material_map.at(ioinstance->material));
  }

  for (auto ioenvironment : ioscene->environments) {
    if (print_progress)
      print_progress("convert environment", progress.x++, progress.y);
    auto environment = add_environment(scene);
    set_frame(environment, ioenvironment->frame);
    set_emission(environment, ioenvironment->emission,
        texture_map.at(ioenvironment->emission_tex));
  }

  // done
  if (print_progress) print_progress("convert done", progress.x++, progress.y);

  // get camera
  camera = camera_map.at(iocamera);
}

void reset_display(app_state* app) {
  // stop render
  app->render_stop = true;
  if (app->render_worker.valid()) app->render_worker.get();

  // init state
  init_state(app->render_state, app->scene, app->camera, app->params);
  if (app->render.imsize() != app->render_state->render.imsize()) {
    app->render.resize(app->render_state->render.imsize());
    app->display.resize(app->render_state->render.imsize());
  }

  // render preview
  auto pstate_guard = std::make_unique<raytrace_state>();
  auto pstate       = pstate_guard.get();
  auto pprms        = app->params;
  pprms.resolution /= app->params.pratio;
  pprms.samples = 1;
  init_state(pstate, app->scene, app->camera, pprms);
  render_samples(pstate, app->scene, app->camera, pprms);
  for (auto j = 0; j < app->render.imsize().y; j++) {
    for (auto i = 0; i < app->render.imsize().x; i++) {
      auto pi = clamp(i / app->params.pratio, 0, pstate->render.imsize().x - 1),
           pj = clamp(j / app->params.pratio, 0, pstate->render.imsize().y - 1);
      app->render[{i, j}]  = pstate->render[{pi, pj}];
      app->display[{i, j}] = tonemap(app->render[{i, j}], app->exposure);
    }
  }

  // start render
  app->render_counter = 0;
  app->render_stop    = false;
  app->render_worker  = run_async([app]() {
    for (auto sample = 0; sample < app->params.samples; sample++) {
      if (app->render_stop) return;
      render_samples(app->render_state, app->scene, app->camera, app->params);
      for (auto j = 0; j < app->render_state->render.imsize().y; j++) {
        for (auto i = 0; i < app->render_state->render.imsize().x; i++) {
          app->render[{i, j}]  = app->render_state->render[{i, j}];
          app->display[{i, j}] = tonemap(app->render[{i, j}], app->exposure);
        }
      }
    }
  });
}

int main(int argc, const char* argv[]) {
  // application
  auto app_guard = std::make_unique<app_state>();
  auto app       = app_guard.get();

  // command line options
  auto camera_name = ""s;
  auto add_skyenv  = false;

  // parse command line
  auto cli = make_cli("yscnitraces", "progressive path tracing");
  add_option(cli, "--camera", camera_name, "Camera name.");
  add_option(
      cli, "--resolution,-r", app->params.resolution, "Image resolution.");
  add_option(cli, "--samples,-s", app->params.samples, "Number of samples.");
  add_option(cli, "--shader,-t", app->params.shader, "Tracer type.",
      raytrace_shader_names);
  add_option(
      cli, "--bounces,-b", app->params.bounces, "Maximum number of bounces.");
  add_option(cli, "--clamp", app->params.clamp, "Final pixel clamping.");
  add_option(cli, "--skyenv/--no-skyenv", add_skyenv, "Add sky envmap");
  add_option(cli, "--output,-o", app->imagename, "Image output");
  add_option(cli, "scene", app->filename, "Scene filename", true);
  parse_cli(cli, argc, argv);

  // scene loading
  auto ioscene_guard = std::make_unique<sceneio_scene>();
  auto ioscene       = ioscene_guard.get();
  auto ioerror       = ""s;
  if (!load_scene(app->filename, ioscene, ioerror, print_progress))
    print_fatal(ioerror);

  // get camera
  auto iocamera = get_camera(ioscene, camera_name);

  // conversion
  init_scene(app->scene, ioscene, app->camera, iocamera, print_progress);

  // cleanup
  if (ioscene_guard) ioscene_guard.reset();

  // build bvh
  init_bvh(app->scene, app->params, print_progress);

  // allocate buffers
  reset_display(app);

  // callbacks
  auto callbacks    = gui_callbacks{};
  callbacks.draw_cb = [app](gui_window* win, const gui_input& input) {
    if (!is_initialized(app->glimage)) init_image(app->glimage);
    if (!app->render_counter)
      set_image(app->glimage, app->display, false, false);
    app->glparams.window      = input.window_size;
    app->glparams.framebuffer = input.framebuffer_viewport;
    std::tie(app->glparams.center, app->glparams.scale) = camera_imview(
        app->glparams.center, app->glparams.scale, app->display.imsize(),
        app->glparams.window, app->glparams.fit);
    draw_image(app->glimage, app->glparams);
    app->render_counter++;
    if (app->render_counter > 10) app->render_counter = 0;
  };
  callbacks.widgets_cb = [app](gui_window* win, const gui_input& input) {
    auto edited = 0;
    // if (draw_combobox(win, "camera", app->iocamera, app->ioscene->cameras)) {
    //   app->camera = get_element(
    //       app->iocamera, app->ioscene->cameras, app->scene->cameras);
    //   edited += 1;
    // }
    auto& tparams = app->params;
    edited += draw_slider(win, "resolution", tparams.resolution, 180, 4096);
    edited += draw_slider(win, "nsamples", tparams.samples, 16, 4096);
    edited += draw_combobox(
        win, "shader", (int&)tparams.shader, raytrace_shader_names);
    edited += draw_slider(win, "nbounces", tparams.bounces, 1, 128);
    edited += draw_slider(win, "pratio", tparams.pratio, 1, 64);
    edited += draw_slider(win, "exposure", app->exposure, -5, 5);
    if (edited) reset_display(app);
  };
  callbacks.uiupdate_cb = [app](gui_window* win, const gui_input& input) {
    if ((input.mouse_left || input.mouse_right) && !input.modifier_alt &&
        !input.widgets_active) {
      auto dolly  = 0.0f;
      auto pan    = zero2f;
      auto rotate = zero2f;
      if (input.mouse_left && !input.modifier_shift)
        rotate = (input.mouse_pos - input.mouse_last) / 100.0f;
      if (input.mouse_right)
        dolly = (input.mouse_pos.x - input.mouse_last.x) / 100.0f;
      if (input.mouse_left && input.modifier_shift)
        pan = (input.mouse_pos - input.mouse_last) * app->camera->focus /
              200.0f;
      pan.x                                            = -pan.x;
      std::tie(app->camera->frame, app->camera->focus) = camera_turntable(
          app->camera->frame, app->camera->focus, rotate, dolly, pan);
      reset_display(app);
    }
  };

  // run ui
  run_ui({1280 + 320, 720}, "yscnitraces", callbacks);

  // done
  return 0;
}

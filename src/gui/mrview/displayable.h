/*
   Copyright 2012 Brain Research Institute, Melbourne, Australia

   Written by J-Donald Tournier and David Raffelt, 07/12/12.

   This file is part of MRtrix.

   MRtrix is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   MRtrix is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with MRtrix.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef __gui_mrview_displayable_h__
#define __gui_mrview_displayable_h__

#include <QAction>
#include <QActionGroup>
#include <QMenu>

#include "math/math.h"
#include "gui/opengl/gl.h"
#include "gui/opengl/shader.h"
#include "gui/mrview/colourmap.h"

#ifdef Complex
# undef Complex
#endif

class QAction;

namespace MR
{
  class ProgressBar;

  namespace GUI
  {

    namespace MRView
    {

      class Window;

      const uint32_t InvertScale = 0x08000000;
      const uint32_t DiscardLower = 0x20000000;
      const uint32_t DiscardUpper = 0x40000000;
      const uint32_t Transparency = 0x80000000;
      const uint32_t Lighting = 0x01000000;
      const uint32_t DiscardLowerEnabled = 0x00100000;
      const uint32_t DiscardUpperEnabled = 0x00200000;
      const uint32_t TransparencyEnabled = 0x00400000;
      const uint32_t LightingEnabled = 0x00800000;

      class Displayable : public QAction
      {
        Q_OBJECT

        public:
          Displayable (const std::string& filename);
          Displayable (Window& window, const std::string& filename);

          virtual ~Displayable ();

          const std::string& get_filename () const {
            return filename;
          }

          float scaling_min () const {
            return display_midpoint - 0.5f * display_range;
          }

          float scaling_max () const {
            return display_midpoint + 0.5f * display_range;
          }

          float scaling_rate () const {
            return 1e-3 * (value_max - value_min);
          }

          float intensity_min () const {
            return value_min;
          }

          float intensity_max () const {
            return value_max;
          }

          void set_windowing (float min, float max) {
            display_range = max - min;
            display_midpoint = 0.5 * (min + max);
            emit scalingChanged();
          }
          void adjust_windowing (const QPoint& p) {
            adjust_windowing (p.x(), p.y());
          }

          void reset_windowing () {
            set_windowing (value_min, value_max);
          }

          void adjust_windowing (float brightness, float contrast) {
            display_midpoint -= 0.0005f * display_range * brightness;
            display_range *= Math::exp (-0.002f * contrast);
            emit scalingChanged();
          }

          uint32_t flags () const { return flags_; }

          void set_allowed_features (bool thresholding, bool transparency, bool lighting) {
            uint32_t cmap = flags_;
            set_bit (cmap, DiscardLowerEnabled, thresholding);
            set_bit (cmap, DiscardUpperEnabled, thresholding);
            set_bit (cmap, TransparencyEnabled, transparency);
            set_bit (cmap, LightingEnabled, lighting);
            flags_ = cmap;
          }


          void set_use_discard_lower (bool yesno) {
            if (!discard_lower_enabled()) return;
            set_bit (DiscardLower, yesno);
          }

          void set_use_discard_upper (bool yesno) {
            if (!discard_upper_enabled()) return;
            set_bit (DiscardUpper, yesno);
          }

          void set_use_transparency (bool yesno) {
            if (!transparency_enabled()) return;
            set_bit (Transparency,  yesno);
          }

          void set_use_lighting (bool yesno) {
            if (!lighting_enabled()) return;
            set_bit (LightingEnabled, yesno);
          }

          void set_invert_scale (bool yesno) {
            set_bit (InvertScale, yesno);
          }

          bool scale_inverted () const {
            return flags_ & InvertScale;
          }

          bool discard_lower_enabled () const {
            return flags_ & DiscardLowerEnabled;
          }

          bool discard_upper_enabled () const {
            return flags_ & DiscardUpperEnabled;
          }

          bool transparency_enabled () const {
            return flags_ & TransparencyEnabled;
          }

          bool lighting_enabled () const {
            return flags_ & LightingEnabled;
          }

          bool use_discard_lower () const {
            return discard_lower_enabled() && ( flags_ & DiscardLower );
          }

          bool use_discard_upper () const {
            return discard_upper_enabled() && ( flags_ & DiscardUpper );
          }

          bool use_transparency () const {
            return transparency_enabled() && ( flags_ & Transparency );
          }

          bool use_lighting () const {
            return lighting_enabled() && ( flags_ & Lighting );
          }


          class Shader : public GL::Shader::Program {
            public:
              virtual std::string fragment_shader_source (const Displayable& object) = 0;
              virtual std::string vertex_shader_source (const Displayable& object) = 0;

              virtual bool need_update (const Displayable& object) const;
              virtual void update (const Displayable& object);

              void start (const Displayable& object) {
                if (*this  == 0 || need_update (object)) 
                  recompile (object);
                GL::Shader::Program::start();
              }
            protected:
              uint32_t flags;
              size_t colourmap;

              void recompile (const Displayable& object) {
                if (*this != 0) 
                  clear();

                update (object);

                GL::Shader::Vertex vertex_shader (vertex_shader_source (object));
                GL::Shader::Fragment fragment_shader (fragment_shader_source (object));

                attach (vertex_shader);
                attach (fragment_shader);
                link();
              }
          };


          std::string declare_shader_variables (const std::string& with_prefix = "") const {
            std::string source =
              "uniform float " + with_prefix+"offset;\n"
              "uniform float " + with_prefix+"scale;\n";
            if (use_discard_lower())
              source += "uniform float " + with_prefix+"lower;\n";
            if (use_discard_upper())
              source += "uniform float " + with_prefix+"upper;\n";
            if (use_transparency()) {
              source += 
                "uniform float " + with_prefix+"alpha_scale;\n"
                "uniform float " + with_prefix+"alpha_offset;\n"
                "uniform float " + with_prefix+"alpha;\n";
            }
            return source;
          }

          void start (Shader& shader_program, float scaling = 1.0, const std::string& with_prefix = "") {
            shader_program.start (*this);
            set_shader_variables (shader_program, scaling, with_prefix);
          }

          void set_shader_variables (Shader& shader_program, float scaling = 1.0, const std::string& with_prefix = "") {
            glUniform1f (glGetUniformLocation (shader_program, (with_prefix+"offset").c_str()), (display_midpoint - 0.5f * display_range) / scaling);
            glUniform1f (glGetUniformLocation (shader_program, (with_prefix+"scale").c_str()), scaling / display_range);
            if (use_discard_lower())
              glUniform1f (glGetUniformLocation (shader_program, (with_prefix+"lower").c_str()), lessthan / scaling);
            if (use_discard_upper())
              glUniform1f (glGetUniformLocation (shader_program, (with_prefix+"upper").c_str()), greaterthan / scaling);
            if (use_transparency()) {
              glUniform1f (glGetUniformLocation (shader_program, (with_prefix+"alpha_scale").c_str()), scaling / (opaque_intensity - transparent_intensity));
              glUniform1f (glGetUniformLocation (shader_program, (with_prefix+"alpha_offset").c_str()), transparent_intensity / scaling);
              glUniform1f (glGetUniformLocation (shader_program, (with_prefix+"alpha").c_str()), alpha);
            }
          }

          void stop (Shader& shader_program) {
            shader_program.stop();
          }

          float lessthan, greaterthan;
          float display_midpoint, display_range;
          float transparent_intensity, opaque_intensity, alpha;
          size_t colourmap;
          bool show;


        signals:
          void scalingChanged ();


        protected:
          const std::string filename;
          float value_min, value_max;
          uint32_t flags_;

          static const char* vertex_shader_source;

          void set_bit (uint32_t& field, uint32_t bit, bool value) {
            if (value) field |= bit;
            else field &= ~bit;
          }

          void set_bit (uint32_t bit, bool value) {
            uint32_t cmap = flags_;
            set_bit (cmap, bit, value);
            flags_ = cmap;
          }

          void update_levels () {
            assert (finite (value_min));
            assert (finite (value_max));
            if (!finite (transparent_intensity)) 
              transparent_intensity = value_min + 0.1 * (value_max - value_min);
            if (!finite (opaque_intensity)) 
              opaque_intensity = value_min + 0.5 * (value_max - value_min);
            if (!finite (alpha)) 
              alpha = 0.5;
            if (!finite (lessthan))
              lessthan = value_min;
            if (!finite (greaterthan))
              greaterthan = value_max;
          }

      };

    }
  }
}

#endif

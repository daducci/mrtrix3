/*
    Copyright 2008 Brain Research Institute, Melbourne, Australia

    Written by Robert E. Smith, 06/02/14.

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


#include <vector>

#include "app.h"
#include "bitset.h"
#include "command.h"
#include "datatype.h"
#include "progressbar.h"
#include "ptr.h"

#include "image/buffer.h"
#include "image/buffer_scratch.h"
#include "image/header.h"
#include "image/loop.h"
#include "image/utils.h"
#include "image/voxel.h"

#include "math/SH.h"


using namespace MR;
using namespace App;


void usage ()
{

  AUTHOR = "Robert E. Smith (r.smith@brain.org.au)";


  DESCRIPTION
    + "examine the values in spherical harmonic images to estimate (and optionally change) the SH basis used."

    + "In previous versions of MRtrix, the convention used for storing spherical harmonic "
      "coefficients was a non-orthonormal basis (the m!=0 coefficients were a factor of "
      "sqrt(2) too large). This error has been rectified in the new MRtrix (assuming that "
      "compilation was performed without the USE_NON_ORTHONORMAL_SH_BASIS symbol defined), "
      "but will cause issues if processing SH data that was generated using an older version "
      "of MRtrix (or vice-versa)."

    + "This command provides a mechanism for testing the basis used in storage of image data "
      "representing a spherical harmonic series per voxel, and allows the user to forcibly "
      "modify the raw image data to conform to the desired basis.";


  ARGUMENTS
    + Argument ("SH", "the input image of SH coefficients.").allow_multiple().type_image_in();


  OPTIONS
    + Option ("force_old", "force the image data to use the old (i.e. non-orthonormal) basis")
    + Option ("force_new", "force the image data to use the new (i.e. orthonormal) basis")
    + Option ("force_native", "force the image data to use the basis under which MRtrix is compiled");

}






template <typename value_type>
void check_and_update (Image::Header& H, const bool force_old, const bool force_new)
{

  const size_t lmax = std::min (Math::SH::LforN (H.dim(3)), size_t(8));

  // Flag which volumes are m==0 and which are not
  const ssize_t N = H.dim(3);
  BitSet mzero_terms (N, false);
  for (size_t l = 2; l <= lmax; l += 2)
    mzero_terms[Math::SH::index (l, 0)] = true;

  value_type multiplier = 1.0;

  typename Image::Buffer<value_type> buffer (H, (force_old || force_new));
  typename Image::Buffer<value_type>::voxel_type v (buffer);

  // Need to mask out voxels where the DC term is zero
  Image::Info info_mask (H);
  info_mask.set_ndim (3);
  info_mask.datatype() = DataType::Bit;
  Image::BufferScratch<bool> mask (info_mask);
  Image::BufferScratch<bool>::voxel_type v_mask (mask);
  size_t voxel_count = 0;
  {
    Image::LoopInOrder loop (v, "Masking image based on DC term...", 0, 3);
    for (loop.start (v, v_mask); loop.ok(); loop.next (v, v_mask)) {
      const value_type value = v.value();
      if (value && std::isfinite (value)) {
        v_mask.value() = true;
        ++voxel_count;
      } else {
        v_mask.value() = false;
      }
    }
  }

  // Get sums independently for each l
 
  // Each order has a different power, and a different number of m!=0 volumes.
  // Therefore, calculate the mean-square intensity for the m==0 and m!=0
  // volumes independently, report ratio for each harmonic order, and the
  // overall ratio over all harmonic orders:
  double mzero_SoS (0.0), mnonzero_MSoS (0.0);
  Ptr<ProgressBar> progress;
  if (App::log_level > 0 && App::log_level < 2)
    progress = new ProgressBar ("Evaluating SH basis of image " + H.name() + "...", N-1);

  for (size_t l = 2; l <= lmax; l += 2) {

    double mzero_sum = 0.0, mnonzero_sum = 0.0;
    Image::LoopInOrder loop (v, 0, 3);
    for (v[3] = ssize_t (Math::SH::NforL(l-2)); v[3] != ssize_t (Math::SH::NforL(l)); ++v[3]) {
      double sum = 0.0;
      for (loop.start (v, v_mask); loop.ok(); loop.next (v, v_mask)) {
        if (v_mask.value())
          sum += Math::pow2 (value_type(v.value()));
      }
      if (mzero_terms[v[3]]) {
        mzero_sum += sum;
        DEBUG ("Volume " + str(v[3]) + ", m==0, sum " + str(sum));
      } else {
        mnonzero_sum += sum;
        DEBUG ("Volume " + str(v[3]) + ", m!=0, sum " + str(sum));
      }
      if (progress)
      ++*progress;
    }

    const double current_mnonzero_MSoS = mnonzero_sum / (2.0 * l);

    mzero_SoS += mzero_sum;
    mnonzero_MSoS += current_mnonzero_MSoS;

    INFO ("SH order " + str(l) + ", ratio of m!=0 to m==0 power: " + str(current_mnonzero_MSoS/mzero_sum) + 
        ", overall m=0 power: " + str (mzero_sum));

  }

  if (progress)
    progress = NULL;

  const double ratio = mnonzero_MSoS / mzero_SoS;
  INFO ("Mean power ratio across SH orders: " + str(ratio));

  // Threshold to make decision on what basis is being used, if unambiguous
  if ((ratio > (5.0/3.0)) && (ratio < (7.0/3.0))) {
    CONSOLE ("Image " + str(H.name()) + " appears to be in the old non-orthonormal basis");
    if (force_new)
      multiplier = 1.0 / M_SQRT2;
  } else if ((ratio > (2.0/3.0)) && (ratio < (4.0/3.0))) {
    CONSOLE ("Image " + str(H.name()) + " appears to be in the new orthonormal basis");
    if (force_old)
      multiplier = M_SQRT2;
  } else {
    throw Exception ("Cannot make unambiguous decision on SH basis of image " + H.name() + " (power ratio = " + str(ratio) + ")");
  }

  if (multiplier != 1.0) {

    Image::LoopInOrder loop (v, 0, 3);
    ProgressBar progress ("Modifying SH basis of image " + H.name() + "...", N-1);
    for (v[3] = 1; v[3] != N; ++v[3]) {
      if (!mzero_terms[v[3]]) {
        for (loop.start (v); loop.ok(); loop.next (v))
          v.value() *= multiplier;
      }
      ++progress;
    }

  } else if (force_old || force_new) {
    INFO ("Image " + H.name() + " already in desired basis; nothing to do");
  }

}









void run ()
{
  bool force_old = get_options ("force_old").size();
  bool force_new = get_options ("force_new").size();
  if (force_old && force_new)
    throw Exception ("Options -force_old and -force_new are mutually exclusive");
  if (get_options ("force_native").size()) {
    if (force_old || force_new)
      throw Exception ("Option -force_native cannot be used in conjunction with one of the other -force options");
#ifndef USE_NON_ORTHONORMAL_SH_BASIS
    INFO ("Forcing to new orthonormal basis (native)");
    force_new = true;
#else
    INFO ("Forcing to old non-orthonormal basis (native)");
    force_old = true;
#endif
  }

  for (std::vector<ParsedArgument>::const_iterator i = argument.begin(); i != argument.end(); ++i) {

    const std::string path = *i;
    Image::Header H (path);
    if (H.ndim() != 4)
      throw Exception ("Image " + H.name() + " is not 4D and therefore cannot be an SH image");
    const size_t lmax = Math::SH::LforN (H.dim(3));
    if (!lmax)
      throw Exception ("Image " + H.name() + " does not contain enough volumes to be an SH image");
    if (Math::SH::NforL (lmax) != size_t(H.dim(3)))
      throw Exception ("Image " + H.name() + " does not contain a number of volumes appropriate for an SH image");
    if (!H.datatype().is_floating_point())
      throw Exception ("Image " + H.name() + " does not use a floating-point format and therefore cannot be an SH image");

    if (H.datatype().bytes() == 4)
      check_and_update<float> (H, force_old, force_new);
    else
      check_and_update<double> (H, force_old, force_new);

  }

};

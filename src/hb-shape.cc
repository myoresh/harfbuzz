/*
 * Copyright © 2009  Red Hat, Inc.
 * Copyright © 2012  Google, Inc.
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Red Hat Author(s): Behdad Esfahbod
 * Google Author(s): Behdad Esfahbod
 */

#include "hb.hh"

#include "hb-shaper.hh"
#include "hb-shape-plan.hh"
#include "hb-buffer.hh"
#include "hb-font.hh"
#include "hb-machinery.hh"
#include "hb-justification.h"


/**
 * SECTION:hb-shape
 * @title: hb-shape
 * @short_description: Conversion of text strings into positioned glyphs
 * @include: hb.h
 *
 * Shaping is the central operation of HarfBuzz. Shaping operates on buffers,
 * which are sequences of Unicode characters that use the same font and have
 * the same text direction, script, and language. After shaping the buffer
 * contains the output glyphs and their positions.
 **/


static inline void free_static_shaper_list ();

static const char *nil_shaper_list[] = {nullptr};

static struct hb_shaper_list_lazy_loader_t : hb_lazy_loader_t<const char *,
							      hb_shaper_list_lazy_loader_t>
{
  static const char ** create ()
  {
    const char **shaper_list = (const char **) hb_calloc (1 + HB_SHAPERS_COUNT, sizeof (const char *));
    if (unlikely (!shaper_list))
      return nullptr;

    const hb_shaper_entry_t *shapers = _hb_shapers_get ();
    unsigned int i;
    for (i = 0; i < HB_SHAPERS_COUNT; i++)
      shaper_list[i] = shapers[i].name;
    shaper_list[i] = nullptr;

    hb_atexit (free_static_shaper_list);

    return shaper_list;
  }
  static void destroy (const char **l)
  { hb_free (l); }
  static const char ** get_null ()
  { return nil_shaper_list; }
} static_shaper_list;

static inline
void free_static_shaper_list ()
{
  static_shaper_list.free_instance ();
}


/**
 * hb_shape_list_shapers:
 *
 * Retrieves the list of shapers supported by HarfBuzz.
 *
 * Return value: (transfer none) (array zero-terminated=1): an array of
 *    constant strings
 *
 * Since: 0.9.2
 **/
const char **
hb_shape_list_shapers ()
{
  return static_shaper_list.get_unconst ();
}


/**
 * hb_shape_full:
 * @font: an #hb_font_t to use for shaping
 * @buffer: an #hb_buffer_t to shape
 * @features: (array length=num_features) (nullable): an array of user
 *    specified #hb_feature_t or %NULL
 * @num_features: the length of @features array
 * @shaper_list: (array zero-terminated=1) (nullable): a %NULL-terminated
 *    array of shapers to use or %NULL
 *
 * See hb_shape() for details. If @shaper_list is not %NULL, the specified
 * shapers will be used in the given order, otherwise the default shapers list
 * will be used.
 *
 * Return value: false if all shapers failed, true otherwise
 *
 * Since: 0.9.2
 **/
hb_bool_t
hb_shape_full (hb_font_t          *font,
	       hb_buffer_t        *buffer,
	       const hb_feature_t *features,
	       unsigned int        num_features,
	       const char * const *shaper_list)
{
  hb_shape_plan_t *shape_plan = hb_shape_plan_create_cached2 (font->face, &buffer->props,
							      features, num_features,
							      font->coords, font->num_coords,
							      shaper_list);
  hb_bool_t res = hb_shape_plan_execute (shape_plan, font, buffer, features, num_features);
  hb_shape_plan_destroy (shape_plan);

  return res;
}

/**
 * hb_shape:
 * @font: an #hb_font_t to use for shaping
 * @buffer: an #hb_buffer_t to shape
 * @features: (array length=num_features) (nullable): an array of user
 *    specified #hb_feature_t or %NULL
 * @num_features: the length of @features array
 *
 * Shapes @buffer using @font turning its Unicode characters content to
 * positioned glyphs. If @features is not %NULL, it will be used to control the
 * features applied during shaping. If two @features have the same tag but
 * overlapping ranges the value of the feature with the higher index takes
 * precedence.
 *
 * Since: 0.9.2
 **/
void
hb_shape (hb_font_t           *font,
	  hb_buffer_t         *buffer,
	  const hb_feature_t  *features,
	  unsigned int         num_features)
{
  hb_shape_full (font, buffer, features, num_features, nullptr);
}

/**
 * hb_justify:
 * @font: an #hb_font_t to use for shaping
 * @buffer: an #hb_buffer_t to shape
 * @num_target_lengths: the length of @target_lengths array
 * @target_lengths: line lengths for justification of buffer over multiple
 *    lines.
 * @features: (array length=num_features) (nullable): an array of user
 *    specified #hb_feature_t or %NULL
 * @num_features: the length of @features array
 *
 * See hb_shape() for details. If the shaped buffer takes more lines than the
 * specified @num_target_lengths, the justified shaping will continue with the
 * last value from @target_lengths.
 *
 **/
void
hb_justify (hb_font_t         *font,
	        hb_buffer_t         *buffer,
			    unsigned int         num_target_lengths,
			    const hb_position_t *target_lengths,
	        const hb_feature_t  *features,
	        unsigned int         num_features)
{
  if (num_target_lengths == 0){
    return;
  }

  /* First pass - perform ordinary shaping without justification */
  hb_shape_full (font, buffer, features, num_features, nullptr);

  hb_codepoint_t space;
  font->get_nominal_glyph (' ', &space);

  hb_split_buffer_to_lines (buffer, num_target_lengths, target_lengths, space);

  /* TODO(iorsh): Implement justification of each line*/
}

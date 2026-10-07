/* Author: Peter Sovietov */

#include <string.h>
#include <math.h>
#include "ayumi.h"
#include "chip_output_resampler.h"

static double ST_dac_table[32][32][32];

static const double AY_dac_table[] = {
  0.0, 0.0,
  0.00999465934234, 0.00999465934234,
  0.0144502937362, 0.0144502937362,
  0.0210574502174, 0.0210574502174,
  0.0307011520562, 0.0307011520562,
  0.0455481803616, 0.0455481803616,
  0.0644998855573, 0.0644998855573,
  0.107362478065, 0.107362478065,
  0.126588845655, 0.126588845655,
  0.20498970016, 0.20498970016,
  0.292210269322, 0.292210269322,
  0.372838941024, 0.372838941024,
  0.492530708782, 0.492530708782,
  0.635324635691, 0.635324635691,
  0.805584802014, 0.805584802014,
  1.0, 1.0
};

static const double YM_dac_table[] = {
  0.0, 0.0,
  0.00465400167849, 0.00772106507973,
  0.0109559777218, 0.0139620050355,
  0.0169985503929, 0.0200198367285,
  0.024368657969, 0.029694056611,
  0.0350652323186, 0.0403906309606,
  0.0485389486534, 0.0583352407111,
  0.0680552376593, 0.0777752346075,
  0.0925154497597, 0.111085679408,
  0.129747463188, 0.148485542077,
  0.17666895552, 0.211551079576,
  0.246387426566, 0.281101701381,
  0.333730067903, 0.400427252613,
  0.467383840696, 0.53443198291,
  0.635172045472, 0.75800717174,
  0.879926756695, 1.0
};

static void generate_dac(double dac[32][32][32]) {
	// Thanks Hatari (copied as-is from Hatari)
	// Cheap hack but we only run this once
	const double MaxVol = 1.0;                 /* Normal Mode Maximum value in table */
	const double FOURTH2 = 1.19;                  /* Fourth root of two from YM2149 */
	const double WARP = 1.666666666666666667;    /* measured as 1.65932 from 46602 */

	double conductance;
	double conductance_[32];
	int	i, j, k;

	/**
	 * YM2149 and R8=1k follows (2^-1/4)^(n-31) better when 2 voices are
	 * summed (A+B or B+C or C+A) rather than individually (A or B or C):
	 *   conductance = 2.0/3.0/(1.0-1.0/WARP)-2.0/3.0;
	 * When taken into consideration with three voices.
	 *
	 * Note that the YM2149 does not use laser trimmed resistances, thus
	 * has offsets that are added and/or multiplied with (2^-1/4)^(n-31).
	 */
	conductance = 2.0 / 3.0 / (1.0 - 1.0 / WARP) - 2.0 / 3.0; /* conductance = 1.0 */

	/**
	 * Because the YM current output (voltage source with series resistances)
	 * is connected to a grounded resistor to develop the output voltage
	 * (instead of a current to voltage converter), the output transfer
	 * function is not linear. Thus:
	 * 2.0*conductance_[n] = 1.0/(1.0-1.0/FOURTH2/(1.0/conductance + 1.0))-1.0;
	 */
	for (i = 31; i >= 1; i--)
	{
		conductance_[i] = conductance / 2.0;
		conductance = 1.0 / (1.0 - 1.0 / FOURTH2 / (1.0 / conductance + 1.0)) - 1.0;
	}
	conductance_[0] = 1.0e-8; /* Avoid divide by zero */

	/**
	 * YM2149 AC + DC components model:
	 * (Note that Maxvol is 65119 in Simoes' table, 65535 in Gerard's)
	 *
	 * Sum the conductances as a function of a voltage divider:
	 * Vout=Vin*Rout/(Rout+Rin)
	 */
	for (i = 0; i < 32; i++)
		for (j = 0; j < 32; j++)
		for (k = 0; k < 32; k++)
		{
			dac[i][j][k] = ((MaxVol * WARP) / (1.0 +
			1.0 / (conductance_[i] + conductance_[j] + conductance_[k])));
		}

	/**
	 * YM2149 DC component model:
	 * R8=1k (pulldown) + YM//1K (pullup) with YM 50% duty PWM
	 * (Note that MaxVol is 46602 in Paulo Simoes Quartet mode table)
	 *
	 *	for (i = 0; i < 32; i++)
	*		for (j = 0; j < 32; j++)
	*			for (k = 0; k < 32; k++)
	*			{
	*				volumetable[i][j][k] = (ymu16)(0.5+(MaxVol*WARP)/(1.0 +
	*					2.0/(conductance_[i]+conductance_[j]+conductance_[k])));
	*			}
   */
}

static void reset_segment(struct ayumi* ay);

static int update_tone(struct ayumi* ay, int index) {
  struct tone_channel* ch = &ay->channels[index];
  ch->tone_counter += 1;
  if (ch->tone_counter >= ch->tone_period) {
    ch->tone_counter = 0;
    ch->tone ^= 1;
  }
  return ch->tone;
}

static int update_noise(struct ayumi* ay) {
  int bit0x3;
  ay->noise_counter += 1;
  if (ay->noise_counter >= (ay->noise_period << 1)) {
    ay->noise_counter = 0;
    bit0x3 = ((ay->noise ^ (ay->noise >> 3)) & 1);
    ay->noise = (ay->noise >> 1) | (bit0x3 << 16);
  }
  return ay->noise & 1;
}

static void slide_up(struct ayumi* ay) {
  ay->envelope += 1;
  if (ay->envelope > 31) {
    ay->envelope_segment ^= 1;
    reset_segment(ay);
  }
}

static void slide_down(struct ayumi* ay) {
  ay->envelope -= 1;
  if (ay->envelope < 0) {
    ay->envelope_segment ^= 1;
    reset_segment(ay);
  }
}

static void hold_top(struct ayumi* ay) {
  (void) ay;
}

static void hold_bottom(struct ayumi* ay) {
  (void) ay;
}

static void (* const Envelopes[][2])(struct ayumi*) = {
  {slide_down, hold_bottom},
  {slide_down, hold_bottom},
  {slide_down, hold_bottom},
  {slide_down, hold_bottom},
  {slide_up, hold_bottom},
  {slide_up, hold_bottom},
  {slide_up, hold_bottom},
  {slide_up, hold_bottom},
  {slide_down, slide_down},
  {slide_down, hold_bottom},
  {slide_down, slide_up},
  {slide_down, hold_top},
  {slide_up, slide_up},
  {slide_up, hold_top},
  {slide_up, slide_down},
  {slide_up, hold_bottom}
};

static void reset_segment(struct ayumi* ay) {
  if (Envelopes[ay->envelope_shape][ay->envelope_segment] == slide_down
    || Envelopes[ay->envelope_shape][ay->envelope_segment] == hold_top) {
    ay->envelope = 31;
    return;
  }
  ay->envelope = 0;
}

int update_envelope(struct ayumi* ay) {
  ay->envelope_counter += 1;
  if (ay->envelope_counter >= ay->envelope_period) {
    ay->envelope_counter = 0;
    Envelopes[ay->envelope_shape][ay->envelope_segment](ay);
  }
  return ay->envelope;
}

static int clamp_fm_semitone(int value) {
  if (value < -127) {
    return -127;
  }
  if (value > 128) {
    return 128;
  }
  return value;
}

static int clamp_fm_period_offset(int value) {
  if (value < -4095) {
    return -4095;
  }
  if (value > 4095) {
    return 4095;
  }
  return value;
}

static int clamp_fm_waveform_value(struct timer_effect_state* te, int value) {
  if (te->fm_offset_mode == TIMER_FM_OFFSET_PERIOD) {
    return clamp_fm_period_offset(value);
  }
  return clamp_fm_semitone(value);
}

static int fm_pwm_enabled(struct timer_effect_state* te) {
  return (te->pwm_mode & TIMER_PWM_MODE_BY_DUTY_INDEX) != 0;
}

static int timer_effect_slot_for_kind(int kind) {
  switch (kind) {
    case TIMER_EFFECT_KIND_VOLUME:
      return TIMER_EFFECT_SLOT_SID;
    case TIMER_EFFECT_KIND_ENVELOPE_SHAPE:
      return TIMER_EFFECT_SLOT_SYNCBUZZER;
    case TIMER_EFFECT_KIND_TONE:
      return TIMER_EFFECT_SLOT_FM;
    case TIMER_EFFECT_KIND_ENVELOPE_PERIOD:
      return TIMER_EFFECT_SLOT_ENV_FM;
    default:
      return TIMER_EFFECT_SLOT_SID;
  }
}

static int timer_effect_kind_for_slot(int slot) {
  switch (slot) {
    case TIMER_EFFECT_SLOT_SID:
      return TIMER_EFFECT_KIND_VOLUME;
    case TIMER_EFFECT_SLOT_SYNCBUZZER:
      return TIMER_EFFECT_KIND_ENVELOPE_SHAPE;
    case TIMER_EFFECT_SLOT_FM:
      return TIMER_EFFECT_KIND_TONE;
    case TIMER_EFFECT_SLOT_ENV_FM:
      return TIMER_EFFECT_KIND_ENVELOPE_PERIOD;
    default:
      return TIMER_EFFECT_KIND_NONE;
  }
}

static struct timer_effect_state* channel_timer_effect(struct tone_channel* ch, int slot) {
  if (slot < 0 || slot >= TIMER_EFFECT_SLOT_COUNT) {
    return &ch->timer_effects[0];
  }
  return &ch->timer_effects[slot];
}

static int timer_effect_uses_fm_waveform_for_slot(int slot) {
  return slot == TIMER_EFFECT_SLOT_FM || slot == TIMER_EFFECT_SLOT_ENV_FM;
}

static int timer_effect_uses_fm_waveform(struct timer_effect_state* te) {
  return te->kind == TIMER_EFFECT_KIND_TONE
    || te->kind == TIMER_EFFECT_KIND_ENVELOPE_PERIOD;
}

static int timer_effect_step_value(struct timer_effect_state* te) {
  if (te->length <= 0) {
    return 0;
  }
  if (te->position < 0 || te->position >= te->length) {
    te->position = 0;
  }
  if (timer_effect_uses_fm_waveform(te)) {
    return clamp_fm_waveform_value(te, te->waveform[te->position]);
  }
  return te->waveform[te->position] & 0xf;
}

static int timer_effect_fm_period(struct timer_effect_state* te, int mask) {
  int semitone;
  int offset;
  double factor;
  int period;
  if (te->position < 0 || te->position >= te->length) {
    te->position = 0;
  }
  if (te->fm_offset_mode == TIMER_FM_OFFSET_PERIOD) {
    offset = clamp_fm_period_offset(te->waveform[te->position]);
    period = te->base_tone_period + offset;
  } else {
    semitone = clamp_fm_semitone(te->waveform[te->position]);
    factor = pow(2.0, -semitone / 12.0);
    period = (int) (llround(te->base_tone_period * factor));
  }
  period &= mask;
  return (period == 0) ? 1 : period;
}

static int timer_effect_tone_period(struct tone_channel* ch) {
  struct timer_effect_state* te = channel_timer_effect(ch, TIMER_EFFECT_SLOT_FM);
  if (!te->enabled || te->length <= 0) {
    return ch->tone_period;
  }
  return timer_effect_fm_period(te, 0xfff);
}

static void apply_timer_effect_tone(struct ayumi* ay, int index) {
  struct tone_channel* ch = &ay->channels[index];
  ayumi_set_tone(ay, index, timer_effect_tone_period(ch));
}

static void apply_timer_effect_envelope_period(struct ayumi* ay, int index) {
  struct timer_effect_state* te = channel_timer_effect(&ay->channels[index], TIMER_EFFECT_SLOT_ENV_FM);
  if (!te->enabled || te->length <= 0) {
    return;
  }
  ayumi_set_envelope(ay, timer_effect_fm_period(te, 0xffff));
}

static int timer_effect_active_period(struct timer_effect_state* te) {
  int active_period;
  int w;
  if (!te->enabled || te->kind == TIMER_EFFECT_KIND_NONE) {
    return 1;
  }
  active_period = te->period;
  if (te->pwm_mode == TIMER_PWM_MODE_BY_STEP_VALUE && te->length > 0) {
    w = timer_effect_step_value(te);
    active_period = (w == 0) ? te->period_low : te->period;
  } else if (fm_pwm_enabled(te) && te->length >= 2) {
    if (te->position < 0 || te->position > 1) {
      te->position = 0;
    }
    active_period = te->position == 0 ? te->period : te->period_low;
  }
  if (active_period <= 0) {
    active_period = 1;
  }
  return active_period;
}

static void timer_effect_advance_position(struct timer_effect_state* te) {
  if (fm_pwm_enabled(te) && te->length >= 2) {
    te->position = (te->position + 1) % 2;
    return;
  }
  if (te->length <= 0) {
    return;
  }
  te->position += 1;
  if (te->position >= te->length) {
    te->position = te->loop >= 0 && te->loop < te->length ? te->loop : 0;
  }
}

static void update_timer_effect_slot(struct ayumi* ay, int index, int slot) {
  struct timer_effect_state* te = channel_timer_effect(&ay->channels[index], slot);
  int active_period;
  if (!te->enabled || te->kind == TIMER_EFFECT_KIND_NONE) {
    return;
  }
  active_period = timer_effect_active_period(te);
  te->counter += 1;
  if (te->counter >= active_period) {
    te->counter = 0;
    if (slot == TIMER_EFFECT_SLOT_SYNCBUZZER) {
      timer_effect_advance_position(te);
      ayumi_set_envelope_shape(ay, timer_effect_step_value(te));
    } else if (slot == TIMER_EFFECT_SLOT_FM) {
      timer_effect_advance_position(te);
      apply_timer_effect_tone(ay, index);
    } else if (slot == TIMER_EFFECT_SLOT_ENV_FM) {
      timer_effect_advance_position(te);
      apply_timer_effect_envelope_period(ay, index);
    } else {
      timer_effect_advance_position(te);
    }
  }
}

static int timer_effect_volume_index(struct ayumi* ay, struct tone_channel* ch) {
  struct timer_effect_state* te = channel_timer_effect(ch, TIMER_EFFECT_SLOT_SID);
  int w;
  int vol;
  if (!te->enabled || te->length <= 0) {
    return ch->volume * 2 + 1;
  }
  w = timer_effect_step_value(te);
  if (w == 0) {
    return 0;
  }
  vol = ay->sid_volume_code[te->base_volume & 0xf][w & 0xf];
  return vol * 2 + 1;
}

static void update_mixer(struct ayumi* ay) {
  int i;
  int out;
  int vol_index;
  int noise = update_noise(ay);
  int envelope = update_envelope(ay);
  ay->left = 0;
  ay->right = 0;
  for (i = 0; i < TONE_CHANNELS; i += 1) {
    struct tone_channel* ch = &ay->channels[i];
    int slot;
    out = (update_tone(ay, i) | ch->t_off) & (noise | ch->n_off);
    for (slot = 0; slot < TIMER_EFFECT_SLOT_COUNT; slot += 1) {
      update_timer_effect_slot(ay, i, slot);
    }
    if (
      channel_timer_effect(ch, TIMER_EFFECT_SLOT_SID)->enabled &&
      !ch->e_on
    ) {
      vol_index = timer_effect_volume_index(ay, ch);
    } else {
      vol_index = ch->e_on ? envelope : ch->volume * 2 + 1;
    }
	ay->channel_volume[i] = out ? vol_index : 0;
    out *= vol_index;
	ay->channel_out[i] = ay->dac_table[out];
	ay->left += ay->channel_out[i] * ch->pan_left;
	ay->right += ay->channel_out[i] * ch->pan_right;
  }
  if (ay->is_st) {
	double st_mix = ST_dac_table[ay->channel_volume[0]][ay->channel_volume[1]][ay->channel_volume[2]] * 2.0;
	ay->left = st_mix;
	ay->right = st_mix;
  }
}

static int nearest_dac_code(const double* dac_table, double target) {
  int best = 0;
  double best_dist = fabs(dac_table[1] - target);
  int code;
  for (code = 0; code < 16; code += 1) {
    double dist = fabs(dac_table[code * 2 + 1] - target);
    if (dist <= best_dist) {
      best = code;
      best_dist = dist;
    }
  }
  return best;
}

static void generate_sid_volume_code(struct ayumi* ay) {
  int v;
  int w;
  for (v = 0; v < 16; v += 1) {
    double volume = ay->dac_table[v * 2 + 1];
    for (w = 0; w < 16; w += 1) {
      double target = volume * ay->dac_table[w * 2 + 1];
      ay->sid_volume_code[v][w] = nearest_dac_code(ay->dac_table, target);
    }
  }
}

int ayumi_configure(struct ayumi* ay, int is_ym, double clock_rate, int sr, int is_st) {
  int i;
  memset(ay, 0, sizeof(struct ayumi));
  chip_output_resampler_bind(
      &ay->resampler,
      AYUMI_RESAMPLER_CHANNELS,
      ay->resampler_y,
      ay->resampler_c,
      ay->resampler_fir,
      ay->resampler_output);
  chip_output_resampler_configure(&ay->resampler, clock_rate / 8.0, (double)sr);
  ay->dac_table = is_ym ? YM_dac_table : AY_dac_table;
  ay->is_st = is_st;
  generate_sid_volume_code(ay);
  if (is_st) {
	generate_dac(ST_dac_table);
  }
  ay->noise = 1;
  ayumi_set_envelope(ay, 1);
  for (i = 0; i < TONE_CHANNELS; i += 1) {
    ayumi_set_tone(ay, i, 1);
  }
  return ay->resampler.step < 1;
}

void ayumi_set_pan(struct ayumi* ay, int index, double pan, int is_eqp) {
  if (is_eqp) {
    ay->channels[index].pan_left = sqrt(1 - pan);
    ay->channels[index].pan_right = sqrt(pan);
  } else {
    ay->channels[index].pan_left = 1 - pan;
    ay->channels[index].pan_right = pan;
  }
}

void ayumi_set_tone(struct ayumi* ay, int index, int period) {
  period &= 0xfff;
  ay->channels[index].tone_period = (period == 0) | period;
}

void ayumi_set_noise(struct ayumi* ay, int period) {
  period &= 0x1f;
  ay->noise_period = (period == 0) | period;
}

void ayumi_set_mixer(struct ayumi* ay, int index, int t_off, int n_off, int e_on) {
  ay->channels[index].t_off = t_off & 1;
  ay->channels[index].n_off = n_off & 1;
  ay->channels[index].e_on = e_on;
}

void ayumi_set_volume(struct ayumi* ay, int index, int volume) {
  ay->channels[index].volume = volume & 0xf;
}

void ayumi_set_timer_effect_slot(struct ayumi* ay, int index, int slot, int enabled, int pwm_mode, int period, int period_low, int base_volume, int base_tone_period, int fm_offset_mode) {
  struct timer_effect_state* te;
  int kind;
  if (index < 0 || index >= TONE_CHANNELS) {
    return;
  }
  if (slot < 0 || slot >= TIMER_EFFECT_SLOT_COUNT) {
    return;
  }
  te = channel_timer_effect(&ay->channels[index], slot);
  kind = timer_effect_kind_for_slot(slot);
  te->enabled = enabled & 1;
  te->kind = kind;
  te->pwm_mode = pwm_mode & 0xf;
  te->fm_offset_mode = fm_offset_mode == TIMER_FM_OFFSET_PERIOD ? TIMER_FM_OFFSET_PERIOD : TIMER_FM_OFFSET_SEMITONE;
  te->period = period <= 0 ? 1 : period;
  te->period_low = period_low <= 0 ? 1 : period_low;
  te->base_volume = base_volume & 0xf;
  te->base_tone_period = base_tone_period
    & (kind == TIMER_EFFECT_KIND_ENVELOPE_PERIOD ? 0xffff : 0xfff);
  if (te->base_tone_period == 0) {
    te->base_tone_period = 1;
  }
  if (te->enabled && slot == TIMER_EFFECT_SLOT_FM && te->length > 0) {
    apply_timer_effect_tone(ay, index);
  }
  if (te->enabled && slot == TIMER_EFFECT_SLOT_ENV_FM) {
    apply_timer_effect_envelope_period(ay, index);
  }
}

void ayumi_set_timer_effect(struct ayumi* ay, int index, int enabled, int kind, int pwm_mode, int period, int period_low, int base_volume, int base_tone_period, int fm_offset_mode) {
  ayumi_set_timer_effect_slot(
    ay,
    index,
    timer_effect_slot_for_kind(kind),
    enabled,
    pwm_mode,
    period,
    period_low,
    base_volume,
    base_tone_period,
    fm_offset_mode
  );
}

void ayumi_set_timer_effect_waveform(struct ayumi* ay, int index, int slot, const int* values, int length, int loop) {
  struct timer_effect_state* te;
  int i;
  int copy_length = length;
  if (index < 0 || index >= TONE_CHANNELS) {
    return;
  }
  if (slot < 0 || slot >= TIMER_EFFECT_SLOT_COUNT) {
    return;
  }
  te = channel_timer_effect(&ay->channels[index], slot);
  if (copy_length > TIMER_EFFECT_WAVEFORM_MAX) {
    copy_length = TIMER_EFFECT_WAVEFORM_MAX;
  }
  if (copy_length < 0) {
    copy_length = 0;
  }
  for (i = 0; i < copy_length; i += 1) {
    if (timer_effect_uses_fm_waveform_for_slot(slot)) {
      te->waveform[i] = clamp_fm_waveform_value(te, values[i]);
    } else {
      te->waveform[i] = values[i] & 0xf;
    }
  }
  te->length = copy_length;
  if (te->length <= 0) {
    te->position = 0;
    te->counter = 0;
    te->loop = 0;
    return;
  }
  if (loop < 0 || loop >= te->length) {
    te->loop = 0;
  } else {
    te->loop = loop;
  }
  if (te->position >= te->length) {
    te->position = 0;
  }
  if (te->enabled && slot == TIMER_EFFECT_SLOT_FM && te->length > 0) {
    apply_timer_effect_tone(ay, index);
  }
  if (te->enabled && slot == TIMER_EFFECT_SLOT_ENV_FM) {
    apply_timer_effect_envelope_period(ay, index);
  }
}

void ayumi_timer_effect_reset(struct ayumi* ay, int index, int slot) {
  struct timer_effect_state* te;
  if (index < 0 || index >= TONE_CHANNELS) {
    return;
  }
  if (slot < 0 || slot >= TIMER_EFFECT_SLOT_COUNT) {
    return;
  }
  te = channel_timer_effect(&ay->channels[index], slot);
  te->counter = 0;
  te->position = 0;
  if (te->enabled && slot == TIMER_EFFECT_SLOT_SYNCBUZZER) {
    ayumi_set_envelope_shape(ay, timer_effect_step_value(te));
  } else if (te->enabled && slot == TIMER_EFFECT_SLOT_FM && te->length > 0) {
    apply_timer_effect_tone(ay, index);
  } else if (te->enabled && slot == TIMER_EFFECT_SLOT_ENV_FM) {
    apply_timer_effect_envelope_period(ay, index);
  }
}

int ayumi_get_timer_effect_active_period(struct ayumi* ay, int index, int slot) {
  struct timer_effect_state* te;
  if (index < 0 || index >= TONE_CHANNELS) {
    return 0;
  }
  if (slot < 0 || slot >= TIMER_EFFECT_SLOT_COUNT) {
    return 0;
  }
  te = channel_timer_effect(&ay->channels[index], slot);
  if (!te->enabled || te->kind == TIMER_EFFECT_KIND_NONE) {
    return 0;
  }
  return timer_effect_active_period(te);
}

void ayumi_get_registers(struct ayumi* ay, unsigned char* out) {
  int i;
  int mixer = 0;
  int tone_period;
  int volume_byte;
  int vol_index;
  struct tone_channel* ch;
  struct timer_effect_state* te;

  for (i = 0; i < 14; i += 1) {
    out[i] = 0;
  }

  for (i = 0; i < TONE_CHANNELS; i += 1) {
    ch = &ay->channels[i];
    tone_period = ch->tone_period & 0xfff;
    out[i * 2] = tone_period & 0xff;
    out[i * 2 + 1] = (tone_period >> 8) & 0x0f;

    if (ch->t_off) {
      mixer |= 1 << i;
    }
    if (ch->n_off) {
      mixer |= 1 << (i + 3);
    }

    if (ch->e_on) {
      volume_byte = (ch->volume & 0xf) | 0x10;
    } else {
      te = channel_timer_effect(ch, TIMER_EFFECT_SLOT_SID);
      if (te->enabled && te->length > 0) {
        vol_index = timer_effect_volume_index(ay, ch);
        volume_byte = vol_index == 0 ? 0 : (vol_index - 1) / 2;
      } else {
        volume_byte = ch->volume & 0xf;
      }
    }
    out[8 + i] = volume_byte & 0xff;
  }

  out[6] = ay->noise_period & 0x1f;
  out[7] = mixer & 0x3f;
  out[11] = ay->envelope_period & 0xff;
  out[12] = (ay->envelope_period >> 8) & 0xff;
  out[13] = ay->envelope_shape & 0x0f;
}

int ayumi_struct_size(void) {
  return (int) sizeof(struct ayumi);
}

void ayumi_set_envelope(struct ayumi* ay, int period) {
  period &= 0xffff;
  ay->envelope_period = (period == 0) | period;
}

void ayumi_set_envelope_shape(struct ayumi* ay, int shape) {
  ay->envelope_shape = shape & 0xf;
  ay->envelope_counter = 0;
  ay->envelope_segment = 0;
  reset_segment(ay);
}

void ayumi_begin_output_frame(struct ayumi* ay) {
  chip_output_resampler_begin_frame(&ay->resampler);
}

void ayumi_output_inner_slot(struct ayumi* ay, int i) {
  int pulls = chip_output_resampler_prepare_slot(&ay->resampler);
  double sample[AYUMI_RESAMPLER_CHANNELS];
  int p;
  for (p = 0; p < pulls; p += 1) {
    update_mixer(ay);
    sample[0] = ay->left;
    sample[1] = ay->right;
    chip_output_resampler_push(&ay->resampler, sample);
  }
  chip_output_resampler_write_slot(&ay->resampler, i);
}

void ayumi_finish_output_frame(struct ayumi* ay) {
  chip_output_resampler_finish_frame(&ay->resampler);
  ay->left = ay->resampler.output[0];
  ay->right = ay->resampler.output[1];
}

void ayumi_process(struct ayumi* ay) {
  int i;
  ayumi_begin_output_frame(ay);
  for (i = DECIMATE_FACTOR - 1; i >= 0; i -= 1) {
    ayumi_output_inner_slot(ay, i);
  }
  ayumi_finish_output_frame(ay);
}

static double dc_filter(struct dc_filter* dc, int index, double x) {
  dc->sum += -dc->delay[index] + x;
  dc->delay[index] = x;
  return x - dc->sum / DC_FILTER_SIZE;
}

void ayumi_remove_dc(struct ayumi* ay) {
  ay->left = dc_filter(&ay->dc_left, ay->dc_index, ay->left);
  ay->right = dc_filter(&ay->dc_right, ay->dc_index, ay->right);
  ay->dc_index = (ay->dc_index + 1) & (DC_FILTER_SIZE - 1);
}

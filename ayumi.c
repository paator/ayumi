/* Author: Peter Sovietov */

#include <string.h>
#include <math.h>
#include "ayumi.h"

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

static int timer_effect_step_value(struct timer_effect_state* te) {
  if (te->length <= 0) {
    return 0;
  }
  if (te->position < 0 || te->position >= te->length) {
    te->position = 0;
  }
  if (te->kind == TIMER_EFFECT_KIND_TONE) {
    return clamp_fm_waveform_value(te, te->waveform[te->position]);
  }
  return te->waveform[te->position] & 0xf;
}

static int timer_effect_tone_period(struct tone_channel* ch) {
  struct timer_effect_state* te = &ch->timer_effect;
  int semitone;
  int offset;
  double factor;
  int period;
  if (!te->enabled || te->kind != TIMER_EFFECT_KIND_TONE || te->length <= 0) {
    return ch->tone_period;
  }
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
  period &= 0xfff;
  return (period == 0) ? 1 : period;
}

static void apply_timer_effect_tone(struct ayumi* ay, int index) {
  struct tone_channel* ch = &ay->channels[index];
  ayumi_set_tone(ay, index, timer_effect_tone_period(ch));
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

static void update_timer_effect(struct ayumi* ay, int index) {
  struct timer_effect_state* te = &ay->channels[index].timer_effect;
  int active_period;
  if (!te->enabled || te->kind == TIMER_EFFECT_KIND_NONE) {
    return;
  }
  active_period = timer_effect_active_period(te);
  te->counter += 1;
  if (te->counter >= active_period) {
    te->counter = 0;
    if (te->kind == TIMER_EFFECT_KIND_ENVELOPE_SHAPE) {
      timer_effect_advance_position(te);
      ayumi_set_envelope_shape(ay, timer_effect_step_value(te));
    } else if (te->kind == TIMER_EFFECT_KIND_TONE) {
      timer_effect_advance_position(te);
      apply_timer_effect_tone(ay, index);
    } else {
      timer_effect_advance_position(te);
    }
  }
}

static int timer_effect_volume_index(struct tone_channel* ch) {
  struct timer_effect_state* te = &ch->timer_effect;
  int w;
  int vol;
  if (!te->enabled || te->kind != TIMER_EFFECT_KIND_VOLUME || te->length <= 0) {
    return ch->volume * 2 + 1;
  }
  w = timer_effect_step_value(te);
  if (w == 0) {
    return 0;
  }
  vol = (w * te->base_volume + 14) / 15;
  if (vol > 15) {
    vol = 15;
  }
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
    out = (update_tone(ay, i) | ch->t_off) & (noise | ch->n_off);
    update_timer_effect(ay, i);
    if (ch->timer_effect.enabled && ch->timer_effect.kind == TIMER_EFFECT_KIND_VOLUME && !ch->e_on) {
      vol_index = timer_effect_volume_index(ch);
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

int ayumi_configure(struct ayumi* ay, int is_ym, double clock_rate, int sr, int is_st) {
  int i;
  memset(ay, 0, sizeof(struct ayumi));
  ay->step = clock_rate / (sr * 8 * DECIMATE_FACTOR);
  ay->dac_table = is_ym ? YM_dac_table : AY_dac_table;
  ay->is_st = is_st;
  if (is_st) {
	generate_dac(ST_dac_table);
  }
  ay->noise = 1;
  ayumi_set_envelope(ay, 1);
  for (i = 0; i < TONE_CHANNELS; i += 1) {
    ayumi_set_tone(ay, i, 1);
  }
  return ay->step < 1;
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

void ayumi_set_timer_effect(struct ayumi* ay, int index, int enabled, int kind, int pwm_mode, int period, int period_low, int base_volume, int base_tone_period, int fm_offset_mode) {
  struct timer_effect_state* te = &ay->channels[index].timer_effect;
  te->enabled = enabled & 1;
  te->kind = kind & 0xf;
  te->pwm_mode = pwm_mode & 0xf;
  te->fm_offset_mode = fm_offset_mode == TIMER_FM_OFFSET_PERIOD ? TIMER_FM_OFFSET_PERIOD : TIMER_FM_OFFSET_SEMITONE;
  te->period = period <= 0 ? 1 : period;
  te->period_low = period_low <= 0 ? 1 : period_low;
  te->base_volume = base_volume & 0xf;
  te->base_tone_period = base_tone_period & 0xfff;
  if (te->base_tone_period == 0) {
    te->base_tone_period = 1;
  }
  if (te->enabled && te->kind == TIMER_EFFECT_KIND_TONE && te->length > 0) {
    apply_timer_effect_tone(ay, index);
  }
}

void ayumi_set_timer_effect_waveform(struct ayumi* ay, int index, const int* values, int length, int loop) {
  struct timer_effect_state* te = &ay->channels[index].timer_effect;
  int i;
  int copy_length = length;
  if (copy_length > TIMER_EFFECT_WAVEFORM_MAX) {
    copy_length = TIMER_EFFECT_WAVEFORM_MAX;
  }
  if (copy_length < 0) {
    copy_length = 0;
  }
  for (i = 0; i < copy_length; i += 1) {
    if (te->kind == TIMER_EFFECT_KIND_TONE) {
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
  if (te->enabled && te->kind == TIMER_EFFECT_KIND_TONE && te->length > 0) {
    apply_timer_effect_tone(ay, index);
  }
}

void ayumi_timer_effect_reset(struct ayumi* ay, int index) {
  struct timer_effect_state* te = &ay->channels[index].timer_effect;
  te->counter = 0;
  te->position = 0;
  if (te->enabled && te->kind == TIMER_EFFECT_KIND_ENVELOPE_SHAPE) {
    ayumi_set_envelope_shape(ay, timer_effect_step_value(te));
  } else if (te->enabled && te->kind == TIMER_EFFECT_KIND_TONE && te->length > 0) {
    apply_timer_effect_tone(ay, index);
  }
}

int ayumi_get_timer_effect_active_period(struct ayumi* ay, int index) {
  struct timer_effect_state* te = &ay->channels[index].timer_effect;
  if (!te->enabled || te->kind == TIMER_EFFECT_KIND_NONE) {
    return 0;
  }
  return timer_effect_active_period(te);
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

static double decimate(double* x) {
  double y = -0.0000046183113992051936 * (x[1] + x[191]) +
    -0.00001117761640887225 * (x[2] + x[190]) +
    -0.000018610264502005432 * (x[3] + x[189]) +
    -0.000025134586135631012 * (x[4] + x[188]) +
    -0.000028494281690666197 * (x[5] + x[187]) +
    -0.000026396828793275159 * (x[6] + x[186]) +
    -0.000017094212558802156 * (x[7] + x[185]) +
    0.000023798193576966866 * (x[9] + x[183]) +
    0.000051281160242202183 * (x[10] + x[182]) +
    0.00007762197826243427 * (x[11] + x[181]) +
    0.000096759426664120416 * (x[12] + x[180]) +
    0.00010240229300393402 * (x[13] + x[179]) +
    0.000089344614218077106 * (x[14] + x[178]) +
    0.000054875700118949183 * (x[15] + x[177]) +
    -0.000069839082210680165 * (x[17] + x[175]) +
    -0.0001447966132360757 * (x[18] + x[174]) +
    -0.00021158452917708308 * (x[19] + x[173]) +
    -0.00025535069106550544 * (x[20] + x[172]) +
    -0.00026228714374322104 * (x[21] + x[171]) +
    -0.00022258805927027799 * (x[22] + x[170]) +
    -0.00013323230495695704 * (x[23] + x[169]) +
    0.00016182578767055206 * (x[25] + x[167]) +
    0.00032846175385096581 * (x[26] + x[166]) +
    0.00047045611576184863 * (x[27] + x[165]) +
    0.00055713851457530944 * (x[28] + x[164]) +
    0.00056212565121518726 * (x[29] + x[163]) +
    0.00046901918553962478 * (x[30] + x[162]) +
    0.00027624866838952986 * (x[31] + x[161]) +
    -0.00032564179486838622 * (x[33] + x[159]) +
    -0.00065182310286710388 * (x[34] + x[158]) +
    -0.00092127787309319298 * (x[35] + x[157]) +
    -0.0010772534348943575 * (x[36] + x[156]) +
    -0.0010737727700273478 * (x[37] + x[155]) +
    -0.00088556645390392634 * (x[38] + x[154]) +
    -0.00051581896090765534 * (x[39] + x[153]) +
    0.00059548767193795277 * (x[41] + x[151]) +
    0.0011803558710661009 * (x[42] + x[150]) +
    0.0016527320270369871 * (x[43] + x[149]) +
    0.0019152679330965555 * (x[44] + x[148]) +
    0.0018927324805381538 * (x[45] + x[147]) +
    0.0015481870327877937 * (x[46] + x[146]) +
    0.00089470695834941306 * (x[47] + x[145]) +
    -0.0010178225878206125 * (x[49] + x[143]) +
    -0.0020037400552054292 * (x[50] + x[142]) +
    -0.0027874356824117317 * (x[51] + x[141]) +
    -0.003210329988021943 * (x[52] + x[140]) +
    -0.0031540624117984395 * (x[53] + x[139]) +
    -0.0025657163651900345 * (x[54] + x[138]) +
    -0.0014750752642111449 * (x[55] + x[137]) +
    0.0016624165446378462 * (x[57] + x[135]) +
    0.0032591192839069179 * (x[58] + x[134]) +
    0.0045165685815867747 * (x[59] + x[133]) +
    0.0051838984346123896 * (x[60] + x[132]) +
    0.0050774264697459933 * (x[61] + x[131]) +
    0.0041192521414141585 * (x[62] + x[130]) +
    0.0023628575417966491 * (x[63] + x[129]) +
    -0.0026543507866759182 * (x[65] + x[127]) +
    -0.0051990251084333425 * (x[66] + x[126]) +
    -0.0072020238234656924 * (x[67] + x[125]) +
    -0.0082672928192007358 * (x[68] + x[124]) +
    -0.0081033739572956287 * (x[69] + x[123]) +
    -0.006583111539570221 * (x[70] + x[122]) +
    -0.0037839040415292386 * (x[71] + x[121]) +
    0.0042781252851152507 * (x[73] + x[119]) +
    0.0084176358598320178 * (x[74] + x[118]) +
    0.01172566057463055 * (x[75] + x[117]) +
    0.013550476647788672 * (x[76] + x[116]) +
    0.013388189369997496 * (x[77] + x[115]) +
    0.010979501242341259 * (x[78] + x[114]) +
    0.006381274941685413 * (x[79] + x[113]) +
    -0.007421229604153888 * (x[81] + x[111]) +
    -0.01486456304340213 * (x[82] + x[110]) +
    -0.021143584622178104 * (x[83] + x[109]) +
    -0.02504275058758609 * (x[84] + x[108]) +
    -0.025473530942547201 * (x[85] + x[107]) +
    -0.021627310017882196 * (x[86] + x[106]) +
    -0.013104323383225543 * (x[87] + x[105]) +
    0.017065133989980476 * (x[89] + x[103]) +
    0.036978919264451952 * (x[90] + x[102]) +
    0.05823318062093958 * (x[91] + x[101]) +
    0.079072012081405949 * (x[92] + x[100]) +
    0.097675998716952317 * (x[93] + x[99]) +
    0.11236045936950932 * (x[94] + x[98]) +
    0.12176343577287731 * (x[95] + x[97]) +
    0.125 * x[96];
  memcpy(&x[FIR_SIZE - DECIMATE_FACTOR], x, DECIMATE_FACTOR * sizeof(double));
  return y;
}

static void ayumi_output_inner_slot_impl(
    struct ayumi* ay, int i, double* fir_left, double* fir_right) {
  double y1;
  double* c_left = ay->interpolator_left.c;
  double* y_left = ay->interpolator_left.y;
  double* c_right = ay->interpolator_right.c;
  double* y_right = ay->interpolator_right.y;
  ay->x += ay->step;
  if (ay->x >= 1) {
    ay->x -= 1;
    y_left[0] = y_left[1];
    y_left[1] = y_left[2];
    y_left[2] = y_left[3];
    y_right[0] = y_right[1];
    y_right[1] = y_right[2];
    y_right[2] = y_right[3];
    update_mixer(ay);
    y_left[3] = ay->left;
    y_right[3] = ay->right;
    y1 = y_left[2] - y_left[0];
    c_left[0] = 0.5 * y_left[1] + 0.25 * (y_left[0] + y_left[2]);
    c_left[1] = 0.5 * y1;
    c_left[2] = 0.25 * (y_left[3] - y_left[1] - y1);
    y1 = y_right[2] - y_right[0];
    c_right[0] = 0.5 * y_right[1] + 0.25 * (y_right[0] + y_right[2]);
    c_right[1] = 0.5 * y1;
    c_right[2] = 0.25 * (y_right[3] - y_right[1] - y1);
  }
  fir_left[i] = (c_left[2] * ay->x + c_left[1]) * ay->x + c_left[0];
  fir_right[i] = (c_right[2] * ay->x + c_right[1]) * ay->x + c_right[0];
}

void ayumi_begin_output_frame(struct ayumi* ay) {
  ay->fir_index = (ay->fir_index + 1) % (FIR_SIZE / DECIMATE_FACTOR - 1);
}

void ayumi_output_inner_slot(struct ayumi* ay, int i) {
  double* fir_left = &ay->fir_left[FIR_SIZE - ay->fir_index * DECIMATE_FACTOR];
  double* fir_right = &ay->fir_right[FIR_SIZE - ay->fir_index * DECIMATE_FACTOR];
  ayumi_output_inner_slot_impl(ay, i, fir_left, fir_right);
}

void ayumi_finish_output_frame(struct ayumi* ay) {
  double* fir_left = &ay->fir_left[FIR_SIZE - ay->fir_index * DECIMATE_FACTOR];
  double* fir_right = &ay->fir_right[FIR_SIZE - ay->fir_index * DECIMATE_FACTOR];
  ay->left = decimate(fir_left);
  ay->right = decimate(fir_right);
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

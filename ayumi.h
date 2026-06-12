/* Author: Peter Sovietov */

#ifndef AYUMI_H
#define AYUMI_H

enum {
  TONE_CHANNELS = 3,
  DECIMATE_FACTOR = 8,
  FIR_SIZE = 192,
  DC_FILTER_SIZE = 1024,
  TIMER_EFFECT_WAVEFORM_MAX = 32
};

enum {
  TIMER_EFFECT_KIND_NONE = 0,
  TIMER_EFFECT_KIND_VOLUME = 1,
  TIMER_EFFECT_KIND_ENVELOPE_SHAPE = 2,
  TIMER_EFFECT_KIND_TONE = 3,
  TIMER_EFFECT_KIND_ENVELOPE_PERIOD = 4
};

enum {
  TIMER_PWM_MODE_OFF = 0,
  TIMER_PWM_MODE_BY_STEP_VALUE = 1,
  TIMER_PWM_MODE_BY_DUTY_INDEX = 2
};

enum {
  TIMER_FM_OFFSET_SEMITONE = 0,
  TIMER_FM_OFFSET_PERIOD = 1
};

enum {
  TIMER_EFFECT_SLOT_SID = 0,
  TIMER_EFFECT_SLOT_SYNCBUZZER = 1,
  TIMER_EFFECT_SLOT_FM = 2,
  TIMER_EFFECT_SLOT_ENV_FM = 3,
  TIMER_EFFECT_SLOT_COUNT = 4
};

struct timer_effect_state {
  int enabled;
  int kind;
  int pwm_mode;
  int fm_offset_mode;
  int period;
  int period_low;
  int counter;
  int base_volume;
  int base_tone_period;
  int waveform[TIMER_EFFECT_WAVEFORM_MAX];
  int length;
  int position;
  int loop;
};

struct tone_channel {
  int tone_period;
  int tone_counter;
  int tone;
  int t_off;
  int n_off;
  int e_on;
  int volume;
  struct timer_effect_state timer_effects[TIMER_EFFECT_SLOT_COUNT];
  double pan_left;
  double pan_right;
};

struct interpolator {
  double c[4];
  double y[4];
};

struct dc_filter {
  double sum;
  double delay[DC_FILTER_SIZE];
};

struct ayumi {
  struct tone_channel channels[TONE_CHANNELS];
  int noise_period;
  int noise_counter;
  int noise;
  int envelope_counter;
  int envelope_period;
  int envelope_shape;
  int envelope_segment;
  int envelope;
  const double* dac_table;
  int is_st;
  int channel_volume[TONE_CHANNELS];
  double step;
  double x;
  struct interpolator interpolator_left;
  struct interpolator interpolator_right;
  double fir_left[FIR_SIZE * 2];
  double fir_right[FIR_SIZE * 2];
  int fir_index;
  struct dc_filter dc_left;
  struct dc_filter dc_right;
  int dc_index;
  double left;
  double right;
  double channel_out[TONE_CHANNELS];
};

int ayumi_configure(struct ayumi* ay, int is_ym, double clock_rate, int sr, int is_st);
void ayumi_set_pan(struct ayumi* ay, int index, double pan, int is_eqp);
void ayumi_set_tone(struct ayumi* ay, int index, int period);
void ayumi_set_noise(struct ayumi* ay, int period);
void ayumi_set_mixer(struct ayumi* ay, int index, int t_off, int n_off, int e_on);
void ayumi_set_volume(struct ayumi* ay, int index, int volume);
void ayumi_set_timer_effect(struct ayumi* ay, int index, int enabled, int kind, int pwm_mode, int period, int period_low, int base_volume, int base_tone_period, int fm_offset_mode);
void ayumi_set_timer_effect_slot(struct ayumi* ay, int index, int slot, int enabled, int pwm_mode, int period, int period_low, int base_volume, int base_tone_period, int fm_offset_mode);
void ayumi_set_timer_effect_waveform(struct ayumi* ay, int index, int slot, const int* values, int length, int loop);
void ayumi_timer_effect_reset(struct ayumi* ay, int index, int slot);
int ayumi_get_timer_effect_active_period(struct ayumi* ay, int index, int slot);
void ayumi_get_registers(struct ayumi* ay, unsigned char* out);
int ayumi_struct_size(void);
void ayumi_set_envelope(struct ayumi* ay, int period);
void ayumi_set_envelope_shape(struct ayumi* ay, int shape);
void ayumi_begin_output_frame(struct ayumi* ay);
void ayumi_output_inner_slot(struct ayumi* ay, int i);
void ayumi_finish_output_frame(struct ayumi* ay);
void ayumi_process(struct ayumi* ay);
void ayumi_remove_dc(struct ayumi* ay);

#endif

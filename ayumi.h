/* Author: Peter Sovietov */

#ifndef AYUMI_H
#define AYUMI_H

enum {
  TONE_CHANNELS = 3,
  DECIMATE_FACTOR = 8,
  FIR_SIZE = 192,
  DC_FILTER_SIZE = 1024,
  SID_WAVEFORM_MAX = 32
};

struct sid_state {
  int enabled;
  int period;
  int counter;
  int position;
  int length;
  int loop;
  int base_volume;
  int waveform[SID_WAVEFORM_MAX];
  int pwm_enabled;
  int period_low;
};

struct syncbuzzer_state {
  int enabled;
  int period;
  int period_low;
  int counter;
  int pwm_enabled;
  int shape;
  int waveform[SID_WAVEFORM_MAX];
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
  struct sid_state sid;
  struct syncbuzzer_state syncbuzzer;
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
void ayumi_set_sid(struct ayumi* ay, int index, int enabled, int period, int base_volume);
void ayumi_set_sid_pwm(struct ayumi* ay, int index, int enabled, int period_high, int period_low, int base_volume);
void ayumi_set_sid_waveform(struct ayumi* ay, int index, const int* values, int length, int loop);
void ayumi_sid_reset(struct ayumi* ay, int index);
void ayumi_set_syncbuzzer(struct ayumi* ay, int index, int enabled, int period, int shape);
void ayumi_set_syncbuzzer_pwm(struct ayumi* ay, int index, int enabled, int period_high, int period_low);
void ayumi_set_syncbuzzer_waveform(struct ayumi* ay, int index, const int* values, int length, int loop);
void ayumi_syncbuzzer_reset(struct ayumi* ay, int index);
int ayumi_get_sid_active_period(struct ayumi* ay, int index);
int ayumi_struct_size(void);
void ayumi_set_envelope(struct ayumi* ay, int period);
void ayumi_set_envelope_shape(struct ayumi* ay, int shape);
void ayumi_begin_output_frame(struct ayumi* ay);
void ayumi_output_inner_slot(struct ayumi* ay, int i);
void ayumi_finish_output_frame(struct ayumi* ay);
void ayumi_process(struct ayumi* ay);
void ayumi_remove_dc(struct ayumi* ay);

#endif



#ifndef _WM_HUBS_H
#define _WM_HUBS_H

struct snd_soc_codec;

extern const unsigned int wm_hubs_spkmix_tlv[];

/* This *must* be the first element of the codec->private_data struct */
struct wm_hubs_data {
	int dcs_codes;
	int dcs_readback_mode;
	int hp_startup_mode;
};

extern int wm_hubs_add_analogue_controls(struct snd_soc_codec *);
extern int wm_hubs_add_analogue_routes(struct snd_soc_codec *, int, int);
extern int wm_hubs_handle_analogue_pdata(struct snd_soc_codec *,
					 int lineout1_diff, int lineout2_diff,
					 int lineout1fb, int lineout2fb,
					 int jd_scthr, int jd_thr,
					 int micbias1_lvl, int micbias2_lvl);

#endif

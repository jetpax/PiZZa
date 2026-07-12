/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- static core binder
 * (CONFIG_RETRO_CORE_STATIC): the core's retro_* symbols are linked
 * into the image (RetroArch console fused-binary model).
 */

#include "retro_core.h"

int retro_core_bind(struct retro_core_api *api)
{
	api->set_environment            = retro_set_environment;
	api->set_video_refresh          = retro_set_video_refresh;
	api->set_audio_sample           = retro_set_audio_sample;
	api->set_audio_sample_batch     = retro_set_audio_sample_batch;
	api->set_input_poll             = retro_set_input_poll;
	api->set_input_state            = retro_set_input_state;
	api->init                       = retro_init;
	api->deinit                     = retro_deinit;
	api->api_version                = retro_api_version;
	api->get_system_info            = retro_get_system_info;
	api->get_system_av_info         = retro_get_system_av_info;
	api->set_controller_port_device = retro_set_controller_port_device;
	api->reset                      = retro_reset;
	api->run                        = retro_run;
	api->serialize_size             = retro_serialize_size;
	api->serialize                  = retro_serialize;
	api->unserialize                = retro_unserialize;
	api->cheat_reset                = retro_cheat_reset;
	api->cheat_set                  = retro_cheat_set;
	api->load_game                  = retro_load_game;
	api->load_game_special          = retro_load_game_special;
	api->unload_game                = retro_unload_game;
	api->get_region                 = retro_get_region;
	api->get_memory_data            = retro_get_memory_data;
	api->get_memory_size            = retro_get_memory_size;
	return 0;
}

void retro_core_unbind(void)
{
}

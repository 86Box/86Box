#ifdef __linux__
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE 1
#endif
#include <chrono>
#include <memory>
#include <stdint.h>
#include <stdlib.h>
#ifdef _WIN32
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif
#include <cstdio>
#include <functional>
#include <array>
#include <vector>
#include <iostream>
#include <string>
#include <string_view>
#include <locale>
#include <algorithm>
#include <utility>
#include <atomic>
#include <thread>
#include <variant>
#include <future> // For non-macOS file picker.
#include "imgui.h"
#include <86box/imgui_settings_window.h>

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/cassette.h>
#include <86box/nvr.h>
#include <86box/machine.h>
#include <86box/gameport.h>
#include <86box/isamem.h>
#include <86box/isartc.h>
#include <86box/lpt.h>
#include <86box/mouse.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/zip.h>
#include <86box/mo.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/network.h>
#include <86box/sound.h>
#include <86box/midi.h>
#include <86box/snd_mpu401.h>
#include <86box/video.h>
#include <86box/plat.h>
#include <86box/plat_midi.h>
#include <86box/ui.h>
#include "../disk/minivhd/minivhd.h"
#include "../disk/minivhd/minivhd_util.h"
}

extern bool OpenFileChooser(char*, size_t, std::vector<std::pair<std::string, std::string>>&, bool);

namespace ImGuiSettingsWindow {

	bool showSettingsWindow = false;
	/* Icon, Bus, File, C, H, S, Size */
	#define C_COLUMNS_HARD_DISKS			6


	static int first_cat = 0;
	static int dpi = 96;

	/* Machine category */
	static int temp_machine_type, temp_machine, temp_cpu, temp_wait_states, temp_fpu, temp_sync;
	static cpu_family_t *temp_cpu_f;
	static uint32_t temp_mem_size;
	#ifdef USE_DYNAREC
	static bool temp_dynarec;
	#endif

	/* Video category */
	static int temp_gfxcard, temp_voodoo;

	/* Input devices category */
	static int temp_mouse, temp_joystick;

	/* Sound category */
	static int temp_sound_card, temp_midi_device, temp_midi_input_device, temp_mpu401, temp_SSI2001, temp_GAMEBLASTER, temp_GUS;
	static int temp_float;

	/* Network category */
	static int temp_net_type, temp_net_card;
	static char temp_pcap_dev[522];

	/* Ports category */
	static int temp_lpt_devices[3];
	static int temp_serial[4], temp_lpt[3];

	/* Other peripherals category */
	static int temp_fdc_card, temp_hdc, temp_ide_ter, temp_ide_qua, temp_cassette;
	static int temp_scsi_card[SCSI_BUS_MAX];
	static int temp_bugger;
	static int temp_postcard;
	static int temp_isartc;
	static int temp_isamem[ISAMEM_MAX];

	static uint8_t temp_deviceconfig;

	/* Hard disks category */
	static hard_disk_t temp_hdd[HDD_NUM];

	/* Floppy drives category */
	static int temp_fdd_types[FDD_NUM];
	static int temp_fdd_turbo[FDD_NUM];
	static int temp_fdd_check_bpb[FDD_NUM];

	/* Other removable devices category */
	static cdrom_t temp_cdrom[CDROM_NUM];
	static zip_drive_t temp_zip_drives[ZIP_NUM];
	static mo_drive_t temp_mo_drives[MO_NUM];

	static uint32_t displayed_category = 0;

	extern int is486;
	static int listtomachinetype[256], listtomachine[256];
	static int listtocpufamily[256], listtocpu[256];
	static int settings_list_to_device[2][256], settings_list_to_fdc[20];
	static int settings_list_to_midi[20], settings_list_to_midi_in[20];
	static int settings_list_to_hdc[20];

	static int max_spt = 63, max_hpc = 255, max_tracks = 266305;
	static uint64_t mfm_tracking, esdi_tracking, xta_tracking, ide_tracking;
	static bool scsi_tracking[64];
	static uint64_t size;
	static int hd_listview_items, hdc_id_to_listview_index[HDD_NUM];
	static int no_update = 0, existing = 0, chs_enabled = 0;
	static int lv1_current_sel, lv2_current_sel;
	static int hard_disk_added = 0, next_free_id = 0, selection = 127;
	static int spt, hpc, tracks, ignore_change = 0;

	static hard_disk_t new_hdd, *hdd_ptr;

	static wchar_t hd_file_name[512];
	static wchar_t device_name[512];

	// forward declares
	void RenderMachineCategory();
	void RenderDisplayCategory();
	void RenderInputCategory();
	void RenderSoundCategory();
	void RenderNetworkCategory();
	void RenderPortsCategory();
	void RenderStorageControllersCategory();
	void RenderHardDisksCategory();
	void RenderFloppyCdromDrivesCategory();
	void RenderOtherRemovableDevicesCategory();
	void RenderOtherPeripheralsCategory();

	bool OpenSettingsFileChooser(char* res, size_t n, const char* filterstr, bool save = false)
	{
		std::vector<std::pair<std::string, std::string>> filefilter{ {"", ""} };
		assert(strstr(filterstr, "|"));
		filefilter[0].first = std::string(filterstr).substr(0, std::string(filterstr).find_first_of('|') - 1);
		filefilter[0].second = std::string(filterstr).substr(std::string(filterstr).find_first_of('|') + 1);
		#ifdef __APPLE__
		return FileOpenSaveMacOSModal(res, n, filefilter, save);
		#else
		auto ok = std::async(std::launch::async | std::launch::deferred, [&res, &n, &filefilter, &save]
		{
			return OpenFileChooser(res, n, filefilter, save);
		});
		while (ok.wait_for(std::chrono::milliseconds(1000 / 60)) != std::future_status::ready)
		{
			SDL_PumpEvents();
			SDL_FlushEvents(0, SDL_LASTEVENT);
		}
		return ok.get();
		#endif
	}
	void InitSettings()
	{
		int i = 0;

		/* Machine category */
		temp_machine_type = machines[machine].type;
		temp_machine = machine;
		temp_cpu_f = cpu_f;
		temp_wait_states = cpu_waitstates;
		temp_cpu = cpu;
		temp_mem_size = mem_size;
	#ifdef USE_DYNAREC
		temp_dynarec = (bool)cpu_use_dynarec;
	#endif
		temp_fpu = fpu_type;
		temp_sync = time_sync;

		/* Video category */
		temp_gfxcard = gfxcard;
		temp_voodoo = voodoo_enabled;

		/* Input devices category */
		temp_mouse = mouse_type;
		temp_joystick = joystick_type;

		/* Sound category */
		temp_sound_card = sound_card_current;
		temp_midi_device = midi_device_current;
		temp_midi_input_device = midi_input_device_current;
		temp_mpu401 = mpu401_standalone_enable;
		temp_SSI2001 = SSI2001;
		temp_GAMEBLASTER = GAMEBLASTER;
		temp_GUS = GUS;
		temp_float = sound_is_float;

		/* Network category */
		temp_net_type = network_type;
		memset(temp_pcap_dev, 0, sizeof(temp_pcap_dev));
	#ifdef ENABLE_SETTINGS_LOG
		assert(sizeof(temp_pcap_dev) == sizeof(network_host));
	#endif
		memcpy(temp_pcap_dev, network_host, sizeof(network_host));
		temp_net_card = network_card;

		/* Ports category */
		for (i = 0; i < 3; i++) {
		temp_lpt_devices[i] = lpt_ports[i].device;
		temp_lpt[i] = lpt_ports[i].enabled;
		}
		for (i = 0; i < 4; i++)
		temp_serial[i] = serial_enabled[i];

		/* Storage devices category */
		for (i = 0; i < SCSI_BUS_MAX; i++)
		temp_scsi_card[i] = scsi_card_current[i];
		temp_fdc_card = fdc_type;
		temp_hdc = hdc_current;
		temp_ide_ter = ide_ter_enabled;
		temp_ide_qua = ide_qua_enabled;
		temp_cassette = cassette_enable;

		mfm_tracking = xta_tracking = esdi_tracking = ide_tracking = 0;
		for (i = 0; i < 64; i++)
		scsi_tracking[i] = 0;

		/* Hard disks category */
		memcpy(temp_hdd, hdd, HDD_NUM * sizeof(hard_disk_t));
		for (i = 0; i < HDD_NUM; i++) {
		if (hdd[i].bus == HDD_BUS_MFM)
			mfm_tracking |= (1 << (hdd[i].mfm_channel << 3));
		else if (hdd[i].bus == HDD_BUS_XTA)
			xta_tracking |= (1 << (hdd[i].xta_channel << 3));
		else if (hdd[i].bus == HDD_BUS_ESDI)
			esdi_tracking |= (1 << (hdd[i].esdi_channel << 3));
		else if ((hdd[i].bus == HDD_BUS_IDE) || (hdd[i].bus == HDD_BUS_ATAPI))
			ide_tracking |= (1 << (hdd[i].ide_channel << 3));
		else if (hdd[i].bus == HDD_BUS_SCSI)
			scsi_tracking[hdd[i].scsi_id] = 1;
		}

		/* Floppy drives category */
		for (i = 0; i < FDD_NUM; i++) {
		temp_fdd_types[i] = fdd_get_type(i);
		temp_fdd_turbo[i] = fdd_get_turbo(i);
		temp_fdd_check_bpb[i] = fdd_get_check_bpb(i);
		}

		/* Other removable devices category */
		memcpy(temp_cdrom, cdrom, CDROM_NUM * sizeof(cdrom_t));
		for (i = 0; i < CDROM_NUM; i++) {
		if (cdrom[i].bus_type == CDROM_BUS_ATAPI)
			ide_tracking |= (2 << (cdrom[i].ide_channel << 3));
		else if (cdrom[i].bus_type == CDROM_BUS_SCSI)
			scsi_tracking[cdrom[i].scsi_device_id] = 1;
		}
		memcpy(temp_zip_drives, zip_drives, ZIP_NUM * sizeof(zip_drive_t));
		for (i = 0; i < ZIP_NUM; i++) {
		if (zip_drives[i].bus_type == ZIP_BUS_ATAPI)
			ide_tracking |= (4 << (zip_drives[i].ide_channel << 3));
		else if (zip_drives[i].bus_type == ZIP_BUS_SCSI)
			scsi_tracking[zip_drives[i].scsi_device_id] = 1;
		}
		memcpy(temp_mo_drives, mo_drives, MO_NUM * sizeof(mo_drive_t));
		for (i = 0; i < MO_NUM; i++) {
		if (mo_drives[i].bus_type == MO_BUS_ATAPI)
		ide_tracking |= (1 << (mo_drives[i].ide_channel << 3));
		else if (mo_drives[i].bus_type == MO_BUS_SCSI)
		scsi_tracking[mo_drives[i].scsi_device_id] = 1;
		}

		/* Other peripherals category */
		temp_bugger = bugger_enabled;
		temp_postcard = postcard_enabled;
		temp_isartc = isartc_type;

		/* ISA memory boards. */
		for (i = 0; i < ISAMEM_MAX; i++)
		temp_isamem[i] = isamem_type[i];	

		temp_deviceconfig = 0;
	}
	static int
	SettingsChanged(void)
	{
		int i = 0, j = 0;

		/* Machine category */
		i = i || (machine != temp_machine);
		i = i || (cpu_f != temp_cpu_f);
		i = i || (cpu_waitstates != temp_wait_states);
		i = i || (cpu != temp_cpu);
		i = i || (mem_size != temp_mem_size);
	#ifdef USE_DYNAREC
		i = i || (temp_dynarec != cpu_use_dynarec);
	#endif
		i = i || (temp_fpu != fpu_type);
		i = i || (temp_sync != time_sync);

		/* Video category */
		i = i || (gfxcard != temp_gfxcard);
		i = i || (voodoo_enabled != temp_voodoo);

		/* Input devices category */
		i = i || (mouse_type != temp_mouse);
		i = i || (joystick_type != temp_joystick);

		/* Sound category */
		i = i || (sound_card_current != temp_sound_card);
		i = i || (midi_device_current != temp_midi_device);
		i = i || (midi_input_device_current != temp_midi_input_device);
		i = i || (mpu401_standalone_enable != temp_mpu401);
		i = i || (SSI2001 != temp_SSI2001);
		i = i || (GAMEBLASTER != temp_GAMEBLASTER);
		i = i || (GUS != temp_GUS);
		i = i || (sound_is_float != temp_float);

		/* Network category */
		i = i || (network_type != temp_net_type);
		i = i || strcmp(temp_pcap_dev, network_host);
		i = i || (network_card != temp_net_card);

		/* Ports category */
		for (j = 0; j < 3; j++) {
		i = i || (temp_lpt_devices[j] != lpt_ports[j].device);
		i = i || (temp_lpt[j] != lpt_ports[j].enabled);
		}
		for (j = 0; j < 4; j++)
		i = i || (temp_serial[j] != serial_enabled[j]);

		/* Storage devices category */
		for (j = 0; j < SCSI_BUS_MAX; j++)
		i = i || (temp_scsi_card[j] != scsi_card_current[j]);
		i = i || (fdc_type != temp_fdc_card);
		i = i || (hdc_current != temp_hdc);
		i = i || (temp_ide_ter != ide_ter_enabled);
		i = i || (temp_ide_qua != ide_qua_enabled);
		i = i || (temp_cassette != cassette_enable);

		/* Hard disks category */
		i = i || memcmp(hdd, temp_hdd, HDD_NUM * sizeof(hard_disk_t));

		/* Floppy drives category */
		for (j = 0; j < FDD_NUM; j++) {
		i = i || (temp_fdd_types[j] != fdd_get_type(j));
		i = i || (temp_fdd_turbo[j] != fdd_get_turbo(j));
		i = i || (temp_fdd_check_bpb[j] != fdd_get_check_bpb(j));
		}

		/* Other removable devices category */
		i = i || memcmp(cdrom, temp_cdrom, CDROM_NUM * sizeof(cdrom_t));
		i = i || memcmp(zip_drives, temp_zip_drives, ZIP_NUM * sizeof(zip_drive_t));
		i = i || memcmp(mo_drives, temp_mo_drives, MO_NUM * sizeof(mo_drive_t));

		/* Other peripherals category */
		i = i || (temp_bugger != bugger_enabled);
		i = i || (temp_postcard != postcard_enabled);
		i = i || (temp_isartc != isartc_type);

		/* ISA memory boards. */
		for (j = 0; j < ISAMEM_MAX; j++)
		i = i || (temp_isamem[j] != isamem_type[j]);

		i = i || !!temp_deviceconfig;

		return i;
	}
	void SaveSettings();

	struct device_config_temp_t
	{
		device_config_t config;
		int val;
		char* str;
		char filestr[512];
	};
	struct
	{
		device_context_t dev;
		std::vector<device_config_temp_t> configs;
		
	} config_device;
	void OpenDeviceWindow(const device_t* device, int inst = 0)
	{
		device_set_context(&config_device.dev, device, inst);
		config_device.configs.clear();
		auto config = config_device.dev.dev->config;
		while (config && config->type != -1)
		{
			config_device.configs.push_back((device_config_temp_t){*config});
			switch (config->type)
			{
				case CONFIG_SELECTION:
				case CONFIG_MIDI:
				case CONFIG_MIDI_IN:
				case CONFIG_SPINNER:
				case CONFIG_BINARY:
					config_device.configs.back().val = config_get_int((char *) config_device.dev.name,
								 (char *) config->name, config->default_int);
					break;
				case CONFIG_HEX16:
					config_device.configs.back().val = config_get_hex16((char *) config_device.dev.name,
								 (char *) config->name, config->default_int);
					break;
				case CONFIG_HEX20:
					config_device.configs.back().val = config_get_hex20((char *) config_device.dev.name,
								 (char *) config->name, config->default_int);
					break;
				case CONFIG_FNAME:
					strncpy(config_device.configs.back().filestr, config_get_string((char *) config_device.dev.name,
								 (char *) config->name, ""), 512);
					break;
			}
			config++;
		}
		ImGui::OpenPopup((std::string(config_device.dev.name) + " device configuration").c_str(), ImGuiPopupFlags_AnyPopupLevel);
	}
	void RenderDeviceWindow()
	{
		if (ImGui::BeginPopupModal((std::string(config_device.dev.name) + " device configuration").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			for (auto &config : config_device.configs)
			{
				//ImGui::TextUnformatted(config.config.description);
				switch (config.config.type)
				{
					case CONFIG_BINARY:
					{
						bool val = config.val == true;
						ImGui::Checkbox(config.config.description, &val);
						config.val = val;
						break;
					}
					case CONFIG_SELECTION:
					case CONFIG_HEX16:
					case CONFIG_HEX20:
					{
						auto selection = config.config.selection;
						auto orig_selection = selection;
						int c = 0;
						while (selection->description && selection->description[0])
						{
							if (selection->value == config.val)
							{
								break;
							}
							selection++;
						}
						if (ImGui::BeginCombo(config.config.description, selection->description))
						{
							selection = orig_selection;
							while (selection->description && selection->description[0])
							{
								if (ImGui::Selectable(selection->description, selection->value == config.val))
								{
									config.val = selection->value;
								}
								if (config.val == selection->value)
								{
									ImGui::SetItemDefaultFocus();
								}
								c++;
								selection++;
							}
							ImGui::EndCombo();
						}
						break;
					}
					case CONFIG_SPINNER:
					{
						ImGui::TextUnformatted(config.config.description); ImGui::SameLine();
						ImGui::InputInt(std::to_string(config.val).c_str(), &config.val, config.config.spinner.step, config.config.spinner.step, ImGuiInputTextFlags_EnterReturnsTrue);
						config.val = (int)std::clamp((int16_t)config.val, config.config.spinner.min, config.config.spinner.max);
						break;
					}
					case CONFIG_MIDI:
					{
						char midiname[512] = { 0 };
						if (config.val >= plat_midi_get_num_devs()) config.val = plat_midi_get_num_devs() - 1;
						plat_midi_get_dev_name(config.val, midiname);
						if (ImGui::BeginCombo(config.config.description, midiname))
						{
							for (int i = 0; i < plat_midi_get_num_devs(); i++)
							{
								plat_midi_get_dev_name(i, midiname);
								if (ImGui::Selectable(midiname, config.val == i))
								{
									config.val = i;
								}
								if (config.val == i)
								{
									ImGui::SetItemDefaultFocus();
								}
							}
							ImGui::EndCombo();
						}
						break;
					}
					case CONFIG_MIDI_IN:
					{
						char midiname[512] = { 0 };
						if (config.val >= plat_midi_in_get_num_devs()) config.val = plat_midi_in_get_num_devs() - 1;
						plat_midi_in_get_dev_name(config.val, midiname);
						if (ImGui::BeginCombo(config.config.description, midiname))
						{
							for (int i = 0; i < plat_midi_in_get_num_devs(); i++)
							{
								plat_midi_in_get_dev_name(i, midiname);
								if (ImGui::Selectable(midiname, config.val == i))
								{
									config.val = i;
								}
								if (config.val == i)
								{
									ImGui::SetItemDefaultFocus();
								}
							}
							ImGui::EndCombo();
						}
						break;
					}
					case CONFIG_FNAME:
					{
						ImGui::TextUnformatted(config.config.description); ImGui::SameLine();
						ImGui::InputText((std::string("##File name") + std::string(config.config.name)).c_str(), (char*)config.filestr, sizeof(config.filestr), ImGuiInputTextFlags_EnterReturnsTrue);
						ImGui::SameLine();
						if (ImGui::Button("..."))
						{
							OpenSettingsFileChooser(config.filestr, sizeof(config.filestr), config.config.file_filter, false);
						}
						break;
					}
				}
			}
			if (ImGui::Button("OK"))
			{
				for (auto& config : config_device.configs)
				{
					switch (config.config.type)
					{
						case CONFIG_SELECTION:
						case CONFIG_MIDI:
						case CONFIG_MIDI_IN:
						case CONFIG_SPINNER:
						case CONFIG_BINARY:
							config_set_int((char *) config_device.dev.name,
								(char *) config.config.name, config.val);
							break;
						case CONFIG_HEX16:
						{
							config_set_hex16((char *) config_device.dev.name,
								(char *) config.config.name, config.val);
							break;
						}
						case CONFIG_HEX20:
						{
							config_set_hex20((char *) config_device.dev.name,
								(char *) config.config.name, config.val);
							break;
						}
						case CONFIG_FNAME:
						{
							config_set_string((char *) config_device.dev.name,
								(char *) config.config.name, config.filestr);
							break;
						}
					}
				}
				temp_deviceconfig |= 1;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) 
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void Render() {
		//ImGui::Begin("Settings", &ImGuiSettingsWindow::showSettingsWindow);
		ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x * 0.95, ImGui::GetIO().DisplaySize.y * 0.95));
		if (!ImGui::BeginPopupModal("Settings Window", &showSettingsWindow, ImGuiWindowFlags_NoResize)) return;

		// Left
		static int currentSettingsCategory = 0;
		{
			ImGui::BeginChild("left pane", ImVec2(150, 0), true);

			if(ImGui::Selectable("Machine")) {
				currentSettingsCategory = 0;
			} else
			if(ImGui::Selectable("Display")) {
				currentSettingsCategory = 1;
			} else
			if(ImGui::Selectable("Input")) {
				currentSettingsCategory = 2;
			} else
			if(ImGui::Selectable("Sound")) {
				currentSettingsCategory = 3;
			} else
			if(ImGui::Selectable("Network")) {
				currentSettingsCategory = 4;
			} else
			if(ImGui::Selectable("Ports (COM & LPT)")) {
				currentSettingsCategory = 5;
			} else
			if(ImGui::Selectable("Storage Controllers")) {
				currentSettingsCategory = 6;
			} else
			if(ImGui::Selectable("Hard Disks")) {
				currentSettingsCategory = 7;
			} else
			if(ImGui::Selectable("Floppy & CD-ROM Drives")) {
				currentSettingsCategory = 8;
			} else
			if(ImGui::Selectable("Other Removable Devices")) {
				currentSettingsCategory = 9;
			} else
			if(ImGui::Selectable("Other Peripherals")) {
				currentSettingsCategory = 10;
			}

			ImGui::EndChild();
		}
		ImGui::SameLine();
		static int save_settings = 0;
		// Right
		{
			ImGui::BeginGroup();
			ImGui::BeginChild("item view", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar); // Leave room for 1 line below us

			ImGui::Separator();

			switch(currentSettingsCategory) {
				case 0: RenderMachineCategory(); break;
				case 1: RenderDisplayCategory(); break;
				case 2: RenderInputCategory(); break;
				case 3: RenderSoundCategory(); break;
				case 4: RenderNetworkCategory(); break;
				case 5: RenderPortsCategory(); break;
				case 6: RenderStorageControllersCategory(); break;
				case 7: RenderHardDisksCategory(); break;
				case 8: RenderFloppyCdromDrivesCategory(); break;
				case 9: RenderOtherRemovableDevicesCategory(); break;
				case 10: RenderOtherPeripheralsCategory(); break;
				default: RenderMachineCategory();
			}

			RenderDeviceWindow();
			ImGui::EndChild();
			
			if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
			ImGui::SameLine();
			if (ImGui::Button("Save"))
			{
				if (!SettingsChanged()) ImGui::CloseCurrentPopup();
				else if (confirm_save) ImGui::OpenPopup("Do you want to save the settings?");
				else save_settings = 1;
			}
			if (ImGui::BeginPopupModal("Do you want to save the settings?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::TextUnformatted("This will hard reset the emulated machine!");
				static bool save_confirm = (!confirm_save);
				ImGui::Checkbox("Don't ask me again", &save_confirm);
				if (ImGui::Button("Yes"))
				{
					confirm_save = !save_confirm;
					save_settings = 1;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("No"))
				{
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			ImGui::EndGroup();
		}
		if (save_settings)
		{
			SaveSettings();
			ImGui::CloseCurrentPopup();
			save_settings = 0;
		}
		ImGui::EndPopup();
		if (!ImGui::IsPopupOpen("Settings Window"))
		{
			plat_pause(0);
		}
	}
	
	void SaveSettings()
	{
		int i = 0;

		pc_reset_hard_close();

		/* Machine category */
		machine = temp_machine;
		cpu_f = temp_cpu_f;
		cpu_waitstates = temp_wait_states;
		cpu = temp_cpu;
		mem_size = temp_mem_size;
	#ifdef USE_DYNAREC
		cpu_use_dynarec = temp_dynarec;
	#endif
		fpu_type = temp_fpu;
		time_sync = temp_sync;

		/* Video category */
		gfxcard = temp_gfxcard;
		voodoo_enabled = temp_voodoo;

		/* Input devices category */
		mouse_type = temp_mouse;
		joystick_type = temp_joystick;

		/* Sound category */
		sound_card_current = temp_sound_card;
		midi_device_current = temp_midi_device;
		midi_input_device_current = temp_midi_input_device;
		mpu401_standalone_enable = temp_mpu401;
		SSI2001 = temp_SSI2001;
		GAMEBLASTER = temp_GAMEBLASTER;
		GUS = temp_GUS;
		sound_is_float = temp_float;

		/* Network category */
		network_type = temp_net_type;
		memset(network_host, '\0', sizeof(network_host));
		strcpy(network_host, temp_pcap_dev);
		network_card = temp_net_card;

		/* Ports category */
		for (i = 0; i < 3; i++) {
		lpt_ports[i].device = temp_lpt_devices[i];
		lpt_ports[i].enabled = temp_lpt[i];
		}
		for (i = 0; i < 4; i++)
		serial_enabled[i] = temp_serial[i];

		/* Storage devices category */
		for (i = 0; i < SCSI_BUS_MAX; i++)
		scsi_card_current[i] = temp_scsi_card[i];
		hdc_current = temp_hdc;
		fdc_type = temp_fdc_card;
		ide_ter_enabled = temp_ide_ter;
		ide_qua_enabled = temp_ide_qua;
		cassette_enable = temp_cassette;

		/* Hard disks category */
		memcpy(hdd, temp_hdd, HDD_NUM * sizeof(hard_disk_t));
		for (i = 0; i < HDD_NUM; i++)
		hdd[i].priv = NULL;

		/* Floppy drives category */
		for (i = 0; i < FDD_NUM; i++) {
		fdd_set_type(i, temp_fdd_types[i]);
		fdd_set_turbo(i, temp_fdd_turbo[i]);
		fdd_set_check_bpb(i, temp_fdd_check_bpb[i]);
		}

		/* Removable devices category */
		memcpy(cdrom, temp_cdrom, CDROM_NUM * sizeof(cdrom_t));
		for (i = 0; i < CDROM_NUM; i++) {
		cdrom[i].img_fp = NULL;
		cdrom[i].priv = NULL;
		cdrom[i].ops = NULL;
		cdrom[i].image = NULL;
		cdrom[i].insert = NULL;
		cdrom[i].close = NULL;
		cdrom[i].get_volume = NULL;
		cdrom[i].get_channel = NULL;
		}
		memcpy(zip_drives, temp_zip_drives, ZIP_NUM * sizeof(zip_drive_t));
		for (i = 0; i < ZIP_NUM; i++) {
		zip_drives[i].f = NULL;
		zip_drives[i].priv = NULL;
		}
		memcpy(mo_drives, temp_mo_drives, MO_NUM * sizeof(mo_drive_t));
		for (i = 0; i < MO_NUM; i++) {
		mo_drives[i].f = NULL;
		mo_drives[i].priv = NULL;
		}

		/* Other peripherals category */
		bugger_enabled = temp_bugger;
		postcard_enabled = temp_postcard;
		isartc_type = temp_isartc;

		/* ISA memory boards. */
		for (i = 0; i < ISAMEM_MAX; i++)
		isamem_type[i] = temp_isamem[i];

		/* Mark configuration as changed. */
		config_changed = 2;

		pc_reset_hard_init();
	}

	void RenderMachineCategory() {

		//ImGui::PushItemWidth(ImGui::GetFontSize() * -12);
		//ImGui::AlignTextToFramePadding();
		struct dev_settings_t
		{
			const char* name;
			size_t id;
		};

		std::vector<char *> item_list; // will be reused for memory savings
		std::vector<dev_settings_t> machine_list;

		//////////////////////////////
		// Machine Type Combo Drop Down Box
		//////////////////////////////
		for (std::size_t i = 0; i < machine_type_count(); ++i) {
			item_list.push_back(machine_type_getname(i));
		}
		const char* machine_type_preview_value = item_list[temp_machine_type];
		ImGui::Text("Machine Type:");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##Machine Type", machine_type_preview_value))
		{
			for (int n = 0; n < item_list.size(); n++)
			{
				const bool is_selected = (temp_machine_type == n);
				if (ImGui::Selectable(item_list.at(n), is_selected))
				{
					temp_machine_type = n;
					for (int i = 0; i < machine_count(); i++)
					{
						if (machine_available(i) && machine_get_type_from_id(i) == n)
						{
							temp_machine = i;
							break;
						}
					}
				}

				// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		//std::cout << "Selected Machine Type: " << item_list.at(machine_type_current) << "\n";

		//////////////////////////////
		// Machine Combo Drop Down
		//////////////////////////////
		ImGui::Text("Machine:");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##Machine", machines[temp_machine].name))
		{
			for (int n = 0; n < machine_count(); n++)
			{
				const bool is_selected = (temp_machine == n);
				if (machine_available(n) && machine_get_type_from_id(n) == temp_machine_type && ImGui::Selectable(machines[n].name, is_selected))
					temp_machine = n;

				// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		auto recalcCPUFamily = [&]()
		{
			if (!cpu_family_is_eligible(temp_cpu_f, temp_machine))
			{
				int c = 0;
				while (cpu_families[c].package != 0)
				{
					if (cpu_family_is_eligible(&cpu_families[c], temp_machine))
					{
						temp_cpu_f = (cpu_family_t *)&cpu_families[c];
						break;
					}
					c++;
				}
			}
		};

		auto recalcCPUSpeed = [&]()
		{
			if (!cpu_is_eligible(temp_cpu_f, temp_cpu, temp_machine))
			{
				int c = 0;
				temp_cpu = temp_cpu_f->cpus[0].cpu_type;
				while (temp_cpu_f->cpus[c].cpu_type != 0)
				{
					if (cpu_is_eligible(temp_cpu_f, c, temp_machine))
					{
						temp_cpu = c;
						break;
					}
					c++;
				}
			}
		};
		//char* machine_name = item_list.size() > 0 ? item_list.at(machine_current) : "";
		machine_t selected_machine = machines[temp_machine];
		recalcCPUFamily();
		recalcCPUSpeed();

		//////////////////////////////
		// CPU Type Combo Drop Down
		//////////////////////////////
		ImGui::Text("CPU:");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##CPU Type", (std::string(temp_cpu_f->manufacturer) + " " + std::string(temp_cpu_f->name)).c_str()))
		{
			int c = 0;
			while (cpu_families[c].package != 0) {
				if (cpu_family_is_eligible(&cpu_families[c], temp_machine))
				{
					if (ImGui::Selectable((std::string(cpu_families[c].manufacturer) + " " + std::string(cpu_families[c].name)).c_str(), &cpu_families[c] == temp_cpu_f))
					temp_cpu_f = (cpu_family_t *)&cpu_families[c];
					
					if (&cpu_families[c] == temp_cpu_f)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				c++;
			}
			ImGui::EndCombo();
		}

		//////////////////////////////
		// CPU Speed Combo Drop Down
		//////////////////////////////
		ImGui::SameLine();
		ImGui::Text("Speed:");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##Speed", temp_cpu_f->cpus[temp_cpu].name))
		{
			int c = 0;
			while (temp_cpu_f->cpus[c].cpu_type != 0) {
				if (cpu_is_eligible(temp_cpu_f, c, temp_machine))
				{
					if (ImGui::Selectable(temp_cpu_f->cpus[c].name, c == temp_cpu))
					temp_cpu = c;
					
					if (temp_cpu == c)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				c++;
			}
			ImGui::EndCombo();
		}

		//////////////////////////////
		// FPU Combo Drop Down
		//////////////////////////////
		if (temp_cpu_f->cpus[temp_cpu].fpus)
		{
			ImGui::Text("FPU:");
			ImGui::SameLine();
			auto getFPUCount = [&]()
			{
				int c = 0;
				while (1)
				{
					if (!fpu_get_name_from_index(temp_cpu_f, temp_cpu, c)) break;
					c++;
				}
				return c;
			};
			auto getFPUIndex = [&]()
			{
				int c = 0;
				size_t i = 0;
				while (1)
				{
					if (c >= getFPUCount()) break;
					if (temp_cpu_f->cpus[temp_cpu].fpus[c].name == NULL) break;
					if (temp_cpu_f->cpus[temp_cpu].fpus[c].type == temp_fpu)
					{
						i = c;
						break;
					}
					c++;
				}
				if (i == 0 || temp_cpu_f->cpus[temp_cpu].fpus[0].type == FPU_INTERNAL) temp_fpu = temp_cpu_f->cpus[temp_cpu].fpus[0].type;
				return i;
			};

			const char* fpustr = temp_cpu_f->cpus[temp_cpu].fpus[getFPUIndex()].name;
			
			if (ImGui::BeginCombo("##FPU", fpustr))
			{
				for (int i = 0; i < getFPUCount(); i++)
				{
					if (ImGui::Selectable(temp_cpu_f->cpus[temp_cpu].fpus[i].name, temp_cpu_f->cpus[temp_cpu].fpus[i].type == temp_fpu))
					{
						temp_fpu = temp_cpu_f->cpus[temp_cpu].fpus[i].type;
					}
					if (temp_fpu == temp_cpu_f->cpus[temp_cpu].fpus[i].type)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
		}
		//////////////////////////////
		// Wait States Combo Drop Down
		//////////////////////////////
		ImGui::BeginDisabled(!(temp_cpu_f->cpus[temp_cpu].cpu_type >= CPU_286 && (temp_cpu_f->cpus[temp_cpu].cpu_type <= CPU_386DX)));
		std::vector<std::string> wait_states_types {"Default"};
		for (int i = 0; i < 8; i++)
		{
			wait_states_types.push_back(std::to_string(i) + std::string(" Wait state(s)"));
		}
		const char* wait_state_preview_value = wait_states_types[temp_wait_states].data();  // Pass in the preview value visible before opening the combo (it could be anything)
		ImGui::Text("Wait States:");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##WaitStates", wait_state_preview_value))
		{
			for (int n = 0; n < wait_states_types.size(); n++)
			{
				const bool is_selected = (temp_wait_states == n);
				if (ImGui::Selectable(wait_states_types[n].data(), is_selected))
					temp_wait_states = n;

				// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
		//////////////////////////////
		// RAM/Memory Config
		//////////////////////////////
		static int memory_amount_mb = temp_mem_size / 1024;
		

		// if (memory_amount > selected_machine.max_ram) {
		// 	memory_amount = selected_machine.max_ram;
		// }

		//std::cout << "Ram Granularity = " << selected_machine.ram_granularity << "\n";
		//std::cout << "Machine: " << selected_machine.name << "\n";

		// ram_granularity is in kilobytes, if it's more than 1024 then step in megabytes
		if (selected_machine.ram_granularity >= 1024) {
			ImGui::Text("Memory (MB):");
			ImGui::SameLine();
			ImGui::InputInt("##memory", &memory_amount_mb, selected_machine.ram_granularity/1024, selected_machine.ram_granularity/1024, ImGuiInputTextFlags_EnterReturnsTrue);
			temp_mem_size = memory_amount_mb * 1024;
			if (temp_mem_size > selected_machine.max_ram) {
				temp_mem_size = selected_machine.max_ram;
			}
			if (temp_mem_size < selected_machine.min_ram) {
				temp_mem_size = selected_machine.min_ram;
			}
			memory_amount_mb = temp_mem_size / 1024;
		}
		else {
			ImGui::Text("Memory (KB):");
			ImGui::SameLine();
			ImGui::InputScalar("##memory", ImGuiDataType_U32, &temp_mem_size, &selected_machine.ram_granularity, &selected_machine.ram_granularity, nullptr, ImGuiInputTextFlags_EnterReturnsTrue);
			if (temp_mem_size > selected_machine.max_ram) {
				temp_mem_size = selected_machine.max_ram;
			}
			if (temp_mem_size < selected_machine.min_ram) {
				temp_mem_size = selected_machine.min_ram;
			}
		}


		// slider style
		// static int memory_amount = 0;
        // ImGui::SliderInt("slider int", &memory_amount, 0, 2000000);

		// dropdown style
		// if (ImGui::BeginCombo("##Memory", memory_config_preview_value))
		// {
		// 	for (int n = 0; n < memory_configs.size(); n++)
		// 	{
		// 		const bool is_selected = (wait_state_current == n);
		// 		if (ImGui::Selectable(memory_configs[n], is_selected))
		// 			memory_config_current = n;

		// 		// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
		// 		if (is_selected)
		// 			ImGui::SetItemDefaultFocus();
		// 	}
		// 	ImGui::EndCombo();
		// }

		//////////////////////////////
		//Dynamic Recompiler Toggle
		//////////////////////////////
		ImGui::BeginDisabled(!(temp_cpu_f->cpus[temp_cpu].cpu_flags & CPU_SUPPORTS_DYNAREC) || (temp_cpu_f->cpus[temp_cpu].cpu_flags & CPU_REQUIRES_DYNAREC));
		if (!(temp_cpu_f->cpus[temp_cpu].cpu_flags & CPU_SUPPORTS_DYNAREC) || (temp_cpu_f->cpus[temp_cpu].cpu_flags & CPU_REQUIRES_DYNAREC))
		{
			if (!(temp_cpu_f->cpus[temp_cpu].cpu_flags & CPU_SUPPORTS_DYNAREC))
				temp_dynarec = 0;
			if (temp_cpu_f->cpus[temp_cpu].cpu_flags & CPU_REQUIRES_DYNAREC)
				temp_dynarec = 1;
		}
		ImGui::Text("Dynamic Recompiler");
		ImGui::SameLine();
		ImGui::Checkbox("##Dynarec", &temp_dynarec);
		ImGui::EndDisabled();

		//////////////////////////////
		// Time Syncronization Radio Selection
		//////////////////////////////

		ImGui::Text("Time Syncronization");
		if (ImGui::RadioButton("Disabled", temp_sync == TIME_SYNC_DISABLED)) {
			temp_sync = TIME_SYNC_DISABLED;
		}
		if (ImGui::RadioButton("Enabled (local time)", temp_sync == TIME_SYNC_ENABLED)) {
			temp_sync = TIME_SYNC_ENABLED;
		}
		if (ImGui::RadioButton("Enabled (UTC)", temp_sync == TIME_SYNC_UTC)) {
			temp_sync = TIME_SYNC_UTC;
		}
	}

	std::string GetNameOfDevice(const device_t *device, char *internal_name, int bus)
	{
		std::string str;
		str.resize(512);
		if (internal_name)
		{
			if (strcmp(internal_name, "none") == 0)
			{
				return "None";
			}
			if (strcmp(internal_name, "internal") == 0)
			{
				return "Internal";
			}
		}
		device_get_name(device, bus, (char*)str.data());
		return str;
	}

	void RenderDisplayCategory() {

		int c = 0;
		ImGui::TextUnformatted("Video card: ");
		if (ImGui::BeginCombo("##Display", GetNameOfDevice(video_card_getdevice(temp_gfxcard), video_get_internal_name(temp_gfxcard), 1).c_str()))
		{
			while (1)
			{
				if ((c == 1) && !(machines[temp_machine].flags & MACHINE_VIDEO)) {
					c++;
					continue;
				}
				auto name = GetNameOfDevice(video_card_getdevice(c), video_get_internal_name(c), 1);
				if (name[0] == 0) break;
				if (video_card_available(c) &&
					device_is_valid(video_card_getdevice(c), machines[temp_machine].flags))
				{
					if (ImGui::Selectable(name.c_str(), c == 0 || c == temp_gfxcard))
					{
						temp_gfxcard = c;
					}
					if ((c == 0) || (c == temp_gfxcard))
						ImGui::SetItemDefaultFocus();
				}
				c++;
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(video_card_getdevice(temp_gfxcard)->config == NULL);
		if (ImGui::Button("Configure"))
		{
			OpenDeviceWindow(video_card_getdevice(temp_gfxcard));
		}
		ImGui::EndDisabled();
		ImGui::BeginDisabled(!(machines[temp_machine].flags & MACHINE_BUS_PCI));
		bool voodoo_enabled = temp_voodoo == true;
		ImGui::Checkbox("Voodoo Graphics", &voodoo_enabled);
		temp_voodoo = voodoo_enabled;
		ImGui::BeginDisabled(!(machines[temp_machine].flags & MACHINE_BUS_PCI) || !temp_voodoo);
		ImGui::SameLine();
		if (ImGui::Button("Configure##Voodoo"))
		{
			OpenDeviceWindow(&voodoo_device);
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
	}
	static int
	mouse_valid(int num, int m)
	{
		const device_t *dev;

		if ((num == MOUSE_TYPE_INTERNAL) &&
		!(machines[m].flags & MACHINE_MOUSE)) return(0);

		dev = mouse_get_device(num);
		return(device_is_valid(dev, machines[m].flags));
	}
	void RenderInputCategory() {
		int c = 0;
		if (!mouse_valid(temp_mouse, temp_machine)) temp_mouse = 0;
		ImGui::TextUnformatted("Mouse:"); ImGui::SameLine();
		if (ImGui::BeginCombo("##Mouse", GetNameOfDevice(mouse_get_device(temp_mouse), mouse_get_internal_name(temp_mouse), 0).c_str()))
		{
			for (c = 0; c < mouse_get_ndev(); c++)
			{
				if (mouse_valid(c, temp_machine))
				{
					if (ImGui::Selectable(GetNameOfDevice(mouse_get_device(c), mouse_get_internal_name(c), 0).c_str(), c == temp_mouse))
					{
						temp_mouse = c;
					}
					if (temp_mouse == c)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
			}
			ImGui::EndCombo();
		}
		ImGui::BeginDisabled(!mouse_has_config(temp_mouse));
		ImGui::SameLine();
		if (ImGui::Button("Configure##Mouse"))
		{
			OpenDeviceWindow(mouse_get_device(temp_mouse));
		}
		ImGui::EndDisabled();
	}
	int
	mpu401_standalone_allow(void)
	{
		char *md, *mdin;

		md = midi_device_get_internal_name(temp_midi_device);
		mdin = midi_in_device_get_internal_name(temp_midi_input_device);

		if (md != NULL) {
		if (!strcmp(md, "none") && !strcmp(mdin, "none"))
			return 0;
		}

		return 1;
	}
	void RenderSoundCategory()
	{
		int c = 0;
		ImGui::TextUnformatted("Sound card:"); ImGui::SameLine();
		if (ImGui::BeginCombo("##Sound", GetNameOfDevice(sound_card_getdevice(temp_sound_card), sound_card_get_internal_name(temp_sound_card), 1).c_str()))
		{
			while (1)
			{
				if ((c == 1) && !(machines[temp_machine].flags & MACHINE_SOUND)) {
					c++;
					continue;
				}
				auto name = GetNameOfDevice(sound_card_getdevice(c), sound_card_get_internal_name(c), 1);
				if (name[0] == 0) break;
				if (sound_card_available(c) &&
					device_is_valid(sound_card_getdevice(c), machines[temp_machine].flags))
				{
					if (ImGui::Selectable(name.c_str(), c == 0 || c == temp_sound_card))
					{
						temp_sound_card = c;
					}
					if ((c == 0) || (c == temp_sound_card))
						ImGui::SetItemDefaultFocus();
				}
				c++;
			}
			ImGui::EndCombo();
		}
		ImGui::BeginDisabled(!sound_card_has_config(temp_sound_card));
		ImGui::SameLine();
		if (ImGui::Button("Configure"))
		{
			OpenDeviceWindow(sound_card_getdevice(temp_sound_card));
		}
		ImGui::EndDisabled();

		ImGui::TextUnformatted("MIDI Out Device:"); ImGui::SameLine();
		if (ImGui::BeginCombo("##MIDI Out Device", GetNameOfDevice(midi_device_getdevice(temp_midi_device), midi_device_get_internal_name(temp_midi_device), 0).c_str()))
		{
			c = 0;
			while (1)
			{
				if (GetNameOfDevice(midi_device_getdevice(c), midi_device_get_internal_name(c), 0)[0] == 0) break;
				
				if (midi_device_available(c))
				{
					if (ImGui::Selectable(GetNameOfDevice(midi_device_getdevice(c), midi_device_get_internal_name(c), 0).c_str(), (c == 0) || (c == temp_midi_device)))
					{
						temp_midi_device = c;
					}
					if ((c == 0) || (c == temp_midi_device))
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				c++;
			}
			ImGui::EndCombo();
		}
		ImGui::BeginDisabled(!midi_device_has_config(temp_midi_device));
		ImGui::SameLine();
		if (ImGui::Button("Configure##MIDI Out Device"))
		{
			OpenDeviceWindow(midi_device_getdevice(temp_midi_device));
		}
		ImGui::EndDisabled();

		ImGui::TextUnformatted("MIDI In Device:"); ImGui::SameLine();
		if (ImGui::BeginCombo("##MIDI In Device", GetNameOfDevice(midi_in_device_getdevice(temp_midi_input_device), midi_in_device_get_internal_name(temp_midi_input_device), 0).c_str()))
		{
			c = 0;
			while (1)
			{
				if (GetNameOfDevice(midi_in_device_getdevice(c), midi_in_device_get_internal_name(c), 0)[0] == 0) break;
				
				if (midi_in_device_available(c))
				{
					if (ImGui::Selectable(GetNameOfDevice(midi_in_device_getdevice(c), midi_in_device_get_internal_name(c), 0).c_str(), (c == 0) || (c == temp_midi_input_device)))
					{
						temp_midi_input_device = c;
					}
					if ((c == 0) || (c == temp_midi_input_device))
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				c++;
			}
			ImGui::EndCombo();
		}
		ImGui::BeginDisabled(!midi_in_device_has_config(temp_midi_input_device));
		ImGui::SameLine();
		if (ImGui::Button("Configure##MIDI In Device"))
		{
			OpenDeviceWindow(midi_in_device_getdevice(temp_midi_input_device));
		}
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!mpu401_standalone_allow());
		bool mpu401_enabled = temp_mpu401;
		ImGui::Checkbox("Standalone MPU-401", &mpu401_enabled);
		temp_mpu401 = mpu401_enabled;
		ImGui::BeginDisabled(!temp_mpu401);
		ImGui::SameLine();
		if (ImGui::Button("Configure##Standalone MPU-401"))
		{
			OpenDeviceWindow((machines[temp_machine].flags & MACHINE_MCA) ? &mpu401_mca_device : &mpu401_device);
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!(machines[temp_machine].flags & MACHINE_BUS_ISA));
		bool ssi_enabled = temp_SSI2001;
		ImGui::Checkbox("Innovation SSI-2001", &ssi_enabled);
		temp_SSI2001 = ssi_enabled;
		ImGui::BeginDisabled(!temp_SSI2001);
		ImGui::SameLine();
		if (ImGui::Button("Configure##Innovation SSI-2001"))
		{
			OpenDeviceWindow(&ssi2001_device);
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!(machines[temp_machine].flags & MACHINE_BUS_ISA));
		bool gb_enabled = temp_GAMEBLASTER;
		ImGui::Checkbox("CMS / Game Blaster", &gb_enabled);
		temp_GAMEBLASTER = gb_enabled;
		ImGui::BeginDisabled(!temp_GAMEBLASTER);
		ImGui::SameLine();
		if (ImGui::Button("Configure##CMS / Game Blaster"))
		{
			OpenDeviceWindow(&cms_device);
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!(machines[temp_machine].flags & MACHINE_BUS_ISA16));
		bool gus_enabled = temp_GUS;
		ImGui::Checkbox("Gravis Ultrasound", &gus_enabled);
		temp_GUS = gus_enabled;
		ImGui::BeginDisabled(!temp_GUS);
		ImGui::SameLine();
		if (ImGui::Button("Configure##Gravis Ultrasound"))
		{
			OpenDeviceWindow(&gus_device);
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		
		bool float32 = temp_float;
		ImGui::Checkbox("Use FLOAT32 sound", &float32);
		temp_float = float32;
	}

	void RenderNetworkCategory() {
		const char* nettypes[] = {"None", "PCap", "SLiRP"};
		ImGui::TextUnformatted("Network type:"); ImGui::SameLine();
		ImGui::Combo("##Network Type", &temp_net_type, nettypes, 3);
		ImGui::BeginDisabled(temp_net_type != NET_TYPE_PCAP);
		ImGui::TextUnformatted("PCap device:"); ImGui::SameLine();
		if (ImGui::BeginCombo("##PCap device:", network_devs[network_dev_to_id(temp_pcap_dev)].description))
		{
			for (int c = 0; c < network_ndev; c++) {
				if (ImGui::Selectable(network_devs[c].description, !strcmp(network_devs[c].device, temp_pcap_dev)))
				{
					strcpy(temp_pcap_dev, network_devs[c].device);
				}
				if (!strcmp(network_devs[c].device, temp_pcap_dev))
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
		ImGui::TextUnformatted("Network adapter:"); ImGui::SameLine();

		ImGui::BeginDisabled(!((temp_net_type == NET_TYPE_SLIRP) ||
				 ((temp_net_type == NET_TYPE_PCAP) && (network_dev_to_id(temp_pcap_dev) > 0))));
		if (ImGui::BeginCombo("##Network adapter", GetNameOfDevice(network_card_getdevice(temp_net_card), network_card_get_internal_name(temp_net_card), 1).c_str()))
		{
			int c = 0;
			while (1)
			{
				auto name = GetNameOfDevice(network_card_getdevice(c), network_card_get_internal_name(c), 1);
				if (name[0] == 0) break;
				if (network_card_available(c) &&
					device_is_valid(network_card_getdevice(c), machines[temp_machine].flags))
				{
					if (ImGui::Selectable(name.c_str(), c == 0 || c == temp_net_card))
					{
						temp_net_card = c;
					}
					if ((c == 0) || (c == temp_net_card))
						ImGui::SetItemDefaultFocus();
				}
				c++;
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::Button("Configure##Network adapter"))
		{
			OpenDeviceWindow(network_card_getdevice(temp_net_card));
		}
		ImGui::EndDisabled();
	}

	void RenderPortsCategory() {
		for (int i = 0; i < 3; i++)
		{
			ImGui::BeginDisabled(!temp_lpt[i]);
			if (ImGui::BeginCombo((std::string("LPT") + std::to_string(i + 1) + " Device").c_str(), lpt_device_get_name(temp_lpt_devices[i])))
			{
				int c = 0;
				while (1)
				{
					if (!lpt_device_get_name(c)) break;
					if (ImGui::Selectable(lpt_device_get_name(c), c == temp_lpt_devices[i]))
					{
						temp_lpt_devices[i] = c;
					}
					if (temp_lpt_devices[i] == c)
					{
						ImGui::SetItemDefaultFocus();
					}
					c++;
				}
				ImGui::EndCombo();
			}
			ImGui::EndDisabled();
		}
		for (int i = 0; i < 4; i++)
		{
			bool temp_serial_b = temp_serial[i];
			if (i != 0 && ((i + 1) % 2)) ImGui::SameLine();
			ImGui::Checkbox((std::string("Serial port ") + std::to_string(i + 1)).c_str(), &temp_serial_b);
			temp_serial[i] = temp_serial_b;
		}
		for (int i = 0; i < 3; i++)
		{
			bool temp_lpt_b = temp_lpt[i];
			if ((i + 1) % 2) ImGui::SameLine();
			ImGui::Checkbox((std::string("Parallel port ") + std::to_string(i + 1)).c_str(), &temp_lpt_b);
			temp_lpt[i] = temp_lpt_b;
		}
	}
	static void RenderDeviceCombo(const char* description, std::function<const device_t*(int)> get_device, std::function<const char*(int)> get_internal_name, std::function<int(int)> available = [](int){ return false; }, int& device = temp_midi_device, int bus = 1, uint32_t flag = 0, bool configure = false, std::function<int(int)> has_config = [](int){ return false; }, int inst = 0)
	{
		int c = 0;
		ImGui::TextUnformatted(description); ImGui::SameLine();
		if (ImGui::BeginCombo((std::string("##") + description).c_str(), GetNameOfDevice(get_device(device), (char*)get_internal_name(device), bus).c_str()))
		{
			while (1)
			{
				if (flag)
				{
					if ((c == 1) && !(machines[temp_machine].flags & flag))
					{
						c++;
						continue;
					}
				}
				auto name = GetNameOfDevice(get_device(c), (char*)get_internal_name(c), bus);
				if (name[0] == 0) break;
				if (available(c) && device_is_valid(get_device(c), machines[temp_machine].flags))
				{
					if (ImGui::Selectable(name.c_str(), c == 0 || c == device))
					{
						device = c;
					}
					if ((c == 0) || (c == device))
						ImGui::SetItemDefaultFocus();
				}
				c++;
			}
			ImGui::EndCombo();
		}
		if (configure)
		{
			ImGui::BeginDisabled(!has_config(device));
			ImGui::SameLine();
			if (ImGui::Button((std::string("Configure") + std::string("##") + description).c_str()))
			{
				OpenDeviceWindow(get_device(device), inst);
			}
			ImGui::EndDisabled();
		}
	}
	void RenderStorageControllersCategory()
	{
		int c = 0;
		RenderDeviceCombo("HD Controller:", hdc_get_device, hdc_get_internal_name, hdc_available, temp_hdc, 1, 0, true, hdc_has_config);
		RenderDeviceCombo("FD Controller:", fdc_card_getdevice, fdc_card_get_internal_name, fdc_card_available, temp_fdc_card, 1, 0, true, fdc_card_has_config);
		
		bool is_at = IS_AT(temp_machine);
		ImGui::BeginDisabled(!is_at);
		{
			bool ide_ter = temp_ide_ter;
			ImGui::Checkbox("Tertiary IDE Controller", &ide_ter);
			temp_ide_ter = ide_ter;
		}
		ImGui::BeginDisabled(!temp_ide_ter);
		ImGui::SameLine();
		if (ImGui::Button("Configure##Tertiary IDE Controller"))
		{
			OpenDeviceWindow(&ide_ter_device);
		}
		ImGui::EndDisabled();
		{
			bool ide_qua = temp_ide_qua;
			ImGui::Checkbox("Quaternary IDE Controller", &ide_qua);
			temp_ide_qua = ide_qua;
		}
		ImGui::BeginDisabled(!temp_ide_qua);
		ImGui::SameLine();
		if (ImGui::Button("Configure##Quaternary IDE Controller"))
		{
			OpenDeviceWindow(&ide_qua_device);
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();

		ImGui::BeginGroup();
		ImGui::TextUnformatted("SCSI");
		for (int i = 0; i < SCSI_BUS_MAX; i++)
		{
			RenderDeviceCombo((std::string("Controller ") + std::to_string(i) + ':').c_str(), scsi_card_getdevice, scsi_card_get_internal_name, scsi_card_available, temp_scsi_card[i], 1, 0, true, scsi_card_has_config, i + 1);
		}
		ImGui::EndGroup();
		{
			bool cas = temp_cassette;
			ImGui::Checkbox("Cassette", &cas);
			temp_cassette = cas;
		}
	}

	static void
	normalize_hd_list()
	{
		hard_disk_t ihdd[HDD_NUM];
		int i, j;

		j = 0;
		memset(ihdd, 0x00, HDD_NUM * sizeof(hard_disk_t));

		for (i = 0; i < HDD_NUM; i++) {
		if (temp_hdd[i].bus != HDD_BUS_DISABLED) {
			memcpy(&(ihdd[j]), &(temp_hdd[i]), sizeof(hard_disk_t));
			j++;
		}
		}

		memcpy(temp_hdd, ihdd, HDD_NUM * sizeof(hard_disk_t));
	}

	static void
	hard_disk_track(uint8_t id)
	{
		switch(temp_hdd[id].bus) {
		case HDD_BUS_MFM:
			mfm_tracking |= (1 << (temp_hdd[id].mfm_channel << 3));
			break;
		case HDD_BUS_ESDI:
			esdi_tracking |= (1 << (temp_hdd[id].esdi_channel << 3));
			break;
		case HDD_BUS_XTA:
			xta_tracking |= (1 << (temp_hdd[id].xta_channel << 3));
			break;
		case HDD_BUS_IDE:
		case HDD_BUS_ATAPI:
			ide_tracking |= (1 << (temp_hdd[id].ide_channel << 3));
			break;
		case HDD_BUS_SCSI:
			scsi_tracking[temp_hdd[id].scsi_id] = 1;
			break;
		}
	}

	static void
	hard_disk_untrack(uint8_t id)
	{
		switch(temp_hdd[id].bus) {
		case HDD_BUS_MFM:
			mfm_tracking &= ~(1 << (temp_hdd[id].mfm_channel << 3));
			break;
		case HDD_BUS_ESDI:
			esdi_tracking &= ~(1 << (temp_hdd[id].esdi_channel << 3));
			break;
		case HDD_BUS_XTA:
			xta_tracking &= ~(1 << (temp_hdd[id].xta_channel << 3));
			break;
		case HDD_BUS_IDE:
		case HDD_BUS_ATAPI:
			ide_tracking &= ~(1 << (temp_hdd[id].ide_channel << 3));
			break;
		case HDD_BUS_SCSI:
			scsi_tracking[temp_hdd[id].scsi_id] = 0;
			break;
		}
	}

	static void
	hard_disk_track_all(void)
	{
		int i;

		for (i = 0; i < HDD_NUM; i++)
		hard_disk_track(i);
	}
	
	static void
	cdrom_track(uint8_t id)
	{
		if (temp_cdrom[id].bus_type == CDROM_BUS_ATAPI)
		ide_tracking |= (2 << (temp_cdrom[id].ide_channel << 3));
		else if (temp_cdrom[id].bus_type == CDROM_BUS_SCSI)
		scsi_tracking[temp_cdrom[id].scsi_device_id] = 1;
	}


	static void
	cdrom_untrack(uint8_t id)
	{
		if (temp_cdrom[id].bus_type == CDROM_BUS_ATAPI)
		ide_tracking &= ~(2 << (temp_cdrom[id].ide_channel << 3));
		else if (temp_cdrom[id].bus_type == CDROM_BUS_SCSI)
		scsi_tracking[temp_cdrom[id].scsi_device_id] = 1;
	}

	static void
	cdrom_track_all(void)
	{
		int i;
		for (i = 0; i < CDROM_NUM; i++)
		cdrom_track(i);
	}

	static void
	zip_track(uint8_t id)
	{
		if (temp_zip_drives[id].bus_type == ZIP_BUS_ATAPI)
		ide_tracking |= (1 << temp_zip_drives[id].ide_channel);
		else if (temp_zip_drives[id].bus_type == ZIP_BUS_SCSI)
		scsi_tracking[temp_zip_drives[id].scsi_device_id] = 1;
	}


	static void
	zip_untrack(uint8_t id)
	{
		if (temp_zip_drives[id].bus_type == ZIP_BUS_ATAPI)
		ide_tracking &= ~(1 << temp_zip_drives[id].ide_channel);
		else if (temp_zip_drives[id].bus_type == ZIP_BUS_SCSI)
		scsi_tracking[temp_zip_drives[id].scsi_device_id] = 0;
	}

	static void
	zip_track_all(void)
	{
		int i;
		for (i = 0; i < ZIP_NUM; i++)
		zip_track(i);
	}

	static void
	mo_track(uint8_t id)
	{
		if (temp_mo_drives[id].bus_type == MO_BUS_ATAPI)
		ide_tracking |= (1 << (temp_zip_drives[id].ide_channel << 3));
		else if (temp_mo_drives[id].bus_type == MO_BUS_SCSI)
		scsi_tracking[temp_mo_drives[id].scsi_device_id] = 1;
	}


	static void
	mo_untrack(uint8_t id)
	{
		if (temp_mo_drives[id].bus_type == MO_BUS_ATAPI)
		ide_tracking &= ~(1 << (temp_zip_drives[id].ide_channel << 3));
		else if (temp_mo_drives[id].bus_type == MO_BUS_SCSI)
		scsi_tracking[temp_mo_drives[id].scsi_device_id] = 0;
	}

	static void
	mo_track_all(void)
	{
		int i;
		for (i = 0; i < MO_NUM; i++)
		mo_track(i);
	}
	
	static int cur_hdd_sel = 0;

	static hard_disk_t* hdd_new = nullptr;
	static uint32_t cur_img_type = 0;
	int CalcNextFreeHDD()
	{
		for (int i = 0; i < HDD_NUM; i++)
		{
			if (temp_hdd[i].bus == HDD_BUS_DISABLED) return i;
		}
		return -1;
	}
	constexpr const std::array<std::string_view, 8> busstr
	{
		"Disabled",
		"MFM/RLL",
		"XTA",
		"ESDI",
		"IDE",
		"ATAPI",
		"SCSI",
		"USB"
	};
	constexpr const std::array<std::string_view, 6> imgstr
	{
		"Raw image (.img)",
		"HDI image (.hdi)",
		"HDX image (.hdx)",
		"Fixed-size VHD (.vhd)",
		"Dynamic-size VHD (.vhd)",
		"Differencing VHD (.vhd)"
	};
	static std::vector<std::string> hddtypes = { "" };

	static uint8_t
	next_free_binary_channel(uint64_t *tracking)
	{
		int64_t i;

		for (i = 0; i < 2; i++) {
		if (!(*tracking & (0xffLL << (i << 3LL))))
			return i;
		}

		return 2;
	}

	static uint8_t
	next_free_ide_channel(void)
	{
		int64_t i;

		for (i = 0; i < 8; i++) {
		if (!(ide_tracking & (0xffLL << (i << 3LL))))
			return i;
		}

		return 7;
	}


	static void
	next_free_scsi_id(uint8_t *id)
	{
		int64_t i;

		for (i = 0; i < 64; i++) {
		if (!scsi_tracking[i]) {
			*id = i;
			return;
		}
		}

		*id = 6;
	}
	void DetermineNextFreeChannel(hard_disk_t& hdd)
	{
		switch (hdd.bus)
		{
			case HDD_BUS_IDE:
			case HDD_BUS_ATAPI:
			{
				hdd.ide_channel = next_free_ide_channel();
				break;
			}
			case HDD_BUS_MFM:
			{
				hdd.mfm_channel = next_free_binary_channel(&mfm_tracking);
				break;
			}
			case HDD_BUS_ESDI:
			{
				hdd.esdi_channel = next_free_binary_channel(&esdi_tracking);
				break;
			}
			case HDD_BUS_XTA:
			{
				hdd.xta_channel = next_free_binary_channel(&xta_tracking);
				break;
			}
			case HDD_BUS_SCSI:
			{
				next_free_scsi_id(&hdd.scsi_id);
				break;
			}
		}
	}
	void DetermineNextFreeChannel(cdrom_t& cd)
	{
		switch (cd.bus_type)
		{
			case CDROM_BUS_ATAPI:
			{
				cd.ide_channel = next_free_ide_channel();
				break;
			}
			case CDROM_BUS_SCSI:
			{
				next_free_scsi_id(&cd.scsi_device_id);
				break;
			}
		}
	}
	void DetermineNextFreeChannel(zip_drive_t& zip)
	{
		switch (zip.bus_type)
		{
			case ZIP_BUS_ATAPI:
			{
				zip.ide_channel = next_free_ide_channel();
				break;
			}
			case ZIP_BUS_SCSI:
			{
				next_free_scsi_id(&zip.scsi_device_id);
				break;
			}
		}
	}
	void DetermineNextFreeChannel(mo_drive_t& mo)
	{
		switch (mo.bus_type)
		{
			case MO_BUS_ATAPI:
			{
				mo.ide_channel = next_free_ide_channel();
				break;
			}
			case MO_BUS_SCSI:
			{
				next_free_scsi_id(&mo.scsi_device_id);
				break;
			}
		}
	}
	static bool hdd_existing = false;
	static bool vhd_block_is_small = false;
	void OpenHDDCreationDialog(bool existing = false)
	{
		hdd_existing = existing;
		hdd_new = new hard_disk_t(temp_hdd[CalcNextFreeHDD()]);
		hdd_new->bus = HDD_BUS_IDE;
		max_hpc = 255;
		max_spt = 63;
		max_tracks = 266305;
		hddtypes.clear();
		for (int i = 0; i < 127; i++)
		{
			char hddtypestr[512] = { 0 };
			snprintf(hddtypestr, sizeof(hddtypestr), "%u MB (CHS: %i, %i, %i)", (hdd_table[i][0] * hdd_table[i][1] * hdd_table[i][2]) >> 11u, hdd_table[i][0], hdd_table[i][1], hdd_table[i][2]);
			hddtypes.emplace_back(hddtypestr);
		}
		hddtypes.push_back("Custom");
		hddtypes.push_back("Custom (large)");
		DetermineNextFreeChannel(*hdd_new);
		ImGui::OpenPopup("Add Hard Disk");
	}
#pragma pack(push, 0)
	struct HDDCreateHDI
	{
		uint32_t zero1 = 0, zero2 = 0;
		uint32_t base = 0x1000, size = 0;
		uint32_t sector_size = 512;
		uint32_t spt = 0, hpc = 0, tracks = 0;
		uint32_t pad[0x3f8];
	};
	struct HDDCreateHDX
	{
		uint64_t signature = 0xD778A82044445459ll;
		uint64_t size = 0;
		uint32_t sector_size = 512;
		uint32_t spt = 0, hpc = 0, tracks = 0;
		uint32_t zero1 = 0, zero2 = 0;
	};
#pragma pack(pop)
	struct HDDCreateVHD
	{
		uint32_t spt = 0, hpc = 0, tracks = 0;
		uint32_t differencing = 0, dynamic = 0;
		uint32_t is_small_block = 0;
		char diff_file[1024];
	};
	static std::atomic<uint32_t> progress{0}, progress_max{1};
	static std::atomic<bool> hdd_creation_ongoing{false};
	static void adjust_86box_geometry_for_vhd(MVHDGeom *_86box_geometry, MVHDGeom *vhd_geometry)
	{
		if (_86box_geometry->cyl <= 65535) {
			vhd_geometry->cyl = _86box_geometry->cyl;
			vhd_geometry->heads = _86box_geometry->heads;
			vhd_geometry->spt = _86box_geometry->spt;
			return;
		}

		int desired_sectors = _86box_geometry->cyl * _86box_geometry->heads * _86box_geometry->spt;
		if (desired_sectors > 267321600)
			desired_sectors = 267321600;

		int remainder = desired_sectors % 85680; /* 8560 is the LCM of 1008 (63*16) and 4080 (255*16) */
		if (remainder > 0)
			desired_sectors += (85680 - remainder);

		_86box_geometry->cyl = desired_sectors / (16 * 63);
		_86box_geometry->heads = 16;
		_86box_geometry->spt = 63;

		vhd_geometry->cyl = desired_sectors / (16 * 255);
		vhd_geometry->heads = 16;
		vhd_geometry->spt = 255;
	}

	static void adjust_vhd_geometry_for_86box(MVHDGeom *vhd_geometry)
	{
		if (vhd_geometry->spt <= 63) 
			return;

		int desired_sectors = vhd_geometry->cyl * vhd_geometry->heads * vhd_geometry->spt;
		if (desired_sectors > 267321600)
			desired_sectors = 267321600;

		int remainder = desired_sectors % 85680; /* 8560 is the LCM of 1008 (63*16) and 4080 (255*16) */
		if (remainder > 0)
			desired_sectors -= remainder;

		vhd_geometry->cyl = desired_sectors / (16 * 63);
		vhd_geometry->heads = 16;
		vhd_geometry->spt = 63;
	}
	static void vhd_progress_callback(uint32_t current_sector, uint32_t total_sectors)
	{
		progress = current_sector;
		progress_max = total_sectors;
	}
	static MVHDGeom create_drive_vhd_fixed(char* filename, int cyl, int heads, int spt)
	{
		MVHDGeom _86box_geometry = { .cyl = (uint16_t)cyl, .heads = (uint8_t)heads, .spt = (uint8_t)spt };
		MVHDGeom vhd_geometry;
		adjust_86box_geometry_for_vhd(&_86box_geometry, &vhd_geometry);

		int vhd_error = 0;
		MVHDMeta *vhd = mvhd_create_fixed(filename, vhd_geometry, &vhd_error, vhd_progress_callback);
		if (vhd == NULL) {
			_86box_geometry.cyl = 0;
			_86box_geometry.heads = 0;
			_86box_geometry.spt = 0;
		} else {
			mvhd_close(vhd);
		}

		return _86box_geometry;
	}
	static MVHDGeom create_drive_vhd_dynamic(char* filename, int cyl, int heads, int spt, int blocksize)
	{
		MVHDGeom _86box_geometry = { .cyl = (uint16_t)cyl, .heads = (uint8_t)heads, .spt = (uint8_t)spt };
		MVHDGeom vhd_geometry;
		adjust_86box_geometry_for_vhd(&_86box_geometry, &vhd_geometry);
		int vhd_error = 0;
		MVHDCreationOptions options;
		options.block_size_in_sectors = blocksize;
		options.path = filename;
		options.size_in_bytes = 0;
		options.geometry = vhd_geometry;
		options.type = MVHD_TYPE_DYNAMIC;

		MVHDMeta *vhd = mvhd_create_ex(options, &vhd_error);
		if (vhd == NULL) {
			_86box_geometry.cyl = 0;
			_86box_geometry.heads = 0;
			_86box_geometry.spt = 0;
		} else {
			mvhd_close(vhd);
		}

		return _86box_geometry;
	}

	static MVHDGeom create_drive_vhd_diff(char* filename, char* parent_filename, int blocksize)
	{
		int vhd_error = 0;
		MVHDCreationOptions options;
		options.block_size_in_sectors = blocksize;
		options.path = filename;
		options.parent_path = parent_filename;
		options.type = MVHD_TYPE_DIFF;

		MVHDMeta *vhd = mvhd_create_ex(options, &vhd_error);
		MVHDGeom vhd_geometry;
		if (vhd == NULL) {
			vhd_geometry.cyl = 0;
			vhd_geometry.heads = 0;
			vhd_geometry.spt = 0;
		} else {
			vhd_geometry = mvhd_get_geometry(vhd);

			if (vhd_geometry.spt > 63) {
				vhd_geometry.cyl = mvhd_calc_size_sectors(&vhd_geometry) / (16 * 63);
				vhd_geometry.heads = 16;
				vhd_geometry.spt = 63;
			}

			mvhd_close(vhd);
		}

		return vhd_geometry;
	}

	bool CreateHDD(char* fn, std::variant<HDDCreateHDI, HDDCreateHDX, HDDCreateVHD, uint64_t> creationparams)
	{
		FILE* f;
		size_t i = 0;
#ifdef _WIN32
		static wchar_t fnwide[260];
		mbstoc16s((uint16_t*)fnwide, fn, 260);
		f = _wfopen(fnwide, L"wb");
#else
		f = fopen(fn, "w");
#endif
		hdd_creation_ongoing = true;
		progress = 0;
		if (!f) { hdd_creation_ongoing = false; return false; }
		else
		{
			if (std::holds_alternative<HDDCreateHDI>(creationparams))
			{
				void* sectsize = calloc(std::get<HDDCreateHDI>(creationparams).sector_size, 2048);
				fwrite((void*)std::addressof(std::get<HDDCreateHDI>(creationparams)), 1, sizeof(HDDCreateHDI), f);
				auto size = std::get<HDDCreateHDI>(creationparams).size;
				auto sectorsize = std::get<HDDCreateHDI>(creationparams).sector_size;
				progress_max = size / sectorsize;
				while (i < (size / sectorsize))
				{
					size_t elemcount = 2048;
					if (((size / sectorsize) - i) < 2048) elemcount = (size / sectorsize) - i;
					fwrite(sectsize, sectorsize, elemcount, f);
					progress += elemcount;
					i += elemcount;
				}
				free(sectsize);
				fclose(f);
				return true;
			}
			else if (std::holds_alternative<HDDCreateHDX>(creationparams))
			{
				void* sectsize = calloc(std::get<HDDCreateHDX>(creationparams).sector_size, 2048);
				fwrite((void*)std::addressof(std::get<HDDCreateHDX>(creationparams)), 1, sizeof(HDDCreateHDX), f);
				auto size = std::get<HDDCreateHDX>(creationparams).size;
				auto sectorsize = std::get<HDDCreateHDX>(creationparams).sector_size;
				progress_max = size / sectorsize;
				while (i < (size / sectorsize))
				{
					size_t elemcount = 2048;
					if (((size / sectorsize) - i) < 2048) elemcount = (size / sectorsize) - i;
					fwrite(sectsize, sectorsize, elemcount, f);
					progress += elemcount;
					i += elemcount;
				}
				free(sectsize);
				fclose(f);
				return true;
			}
			else if (std::holds_alternative<HDDCreateVHD>(creationparams))
			{
				fclose(f);
				f = nullptr;
				auto hddparams = std::get<HDDCreateVHD>(creationparams);
				if (hddparams.differencing)
				{
					auto _86box_geometry = create_drive_vhd_diff(fn, hddparams.diff_file, !hddparams.is_small_block ? MVHD_BLOCK_LARGE : MVHD_BLOCK_SMALL);
				}
				else if (hddparams.dynamic)
				{
					auto _86box_geometry = create_drive_vhd_dynamic(fn, hddparams.tracks, hddparams.hpc, hddparams.spt, !hddparams.is_small_block ? MVHD_BLOCK_LARGE : MVHD_BLOCK_SMALL);
				}
				else
				{
					auto _86box_geometry = create_drive_vhd_fixed(fn, hddparams.tracks, hddparams.hpc, hddparams.spt);
				}
			}
			else
			{
				auto size = std::get<uint64_t>(creationparams);
				void* sectsize = calloc(512, 2048);
				progress_max = size / 512;
				while (i < (size / 512))
				{
					size_t elemcount = 2048;
					if (((size / 512) - i) < 2048) elemcount = (size / 512) - i;
					fwrite(sectsize, 512, elemcount, f);
					progress += elemcount;
					i += elemcount;
				}
				free(sectsize);
				fclose(f);
				return true;
			}
		};
		hdd_creation_ongoing = false;
		return false;
	}
	static std::future<bool> funcresult;
	int getHddType(uint32_t cyl, uint32_t head, uint32_t sect)
	{
		for (int i = 0; i < 127; i++)
		{
			if (hdd_table[i][0] == cyl && hdd_table[i][1] == head && hdd_table[i][2] == sect)
			{
				return i;
			}
		}
		return head == 16 && sect == 63 ? 128 : 127;
	}
	bool ParseHDD(char* fn)
	{
		FILE* f;
		int vhd_error = 0;
#ifdef _WIN32
		static wchar_t fnwide[260];
		mbstoc16s((uint16_t*)fnwide, fn, 260);
		f = _wfopen(fnwide, hdd_existing ? L"rb" : L"wb");
#else
		f = fopen(fn, hdd_existing ? "r" : "w");
#endif
		char* openfilestring = fn;
		if (!f) return false;
        if (image_is_hdi(openfilestring) ||
            image_is_hdx(openfilestring, 1))
		{
			int sector_size = 0;
			fseeko64(f, 0x10, SEEK_SET);
			fread(&sector_size, 1, 4, f);
			if (sector_size != 512)
			{
				ui_msgbox_header(MBX_ERROR, (wchar_t *)IDS_4119,
								(wchar_t *)IDS_4109);
				fclose(f);
				return false;
			}
			spt = hpc = tracks = 0;
			fread(&spt, 1, 4, f);
			fread(&hpc, 1, 4, f);
			fread(&tracks, 1, 4, f);
        }
		else if (image_is_vhd(openfilestring, 1))
		{
			fclose(f);
			MVHDMeta *vhd = mvhd_open(openfilestring, 0, &vhd_error);
			if (vhd == NULL)
			{
				ui_msgbox_header(MBX_ERROR,
									(hdd_existing & 1) ? (wchar_t *)IDS_4114
													: (wchar_t *)IDS_4115,
									(hdd_existing & 1)
										? (wchar_t *)IDS_4107
										: (wchar_t *)IDS_4108);
				return false;
			}
			else if (vhd_error == MVHD_ERR_TIMESTAMP)
			{
				if (ui_msgbox_ex(MBX_QUESTION_YN | MBX_WARNING,
									plat_get_string(IDS_4133),
									plat_get_string(IDS_4132), NULL,
									NULL, NULL) != 0)
				{
					int ts_res =
						mvhd_diff_update_par_timestamp(vhd, &vhd_error);
					if (ts_res != 0)
					{
						ui_msgbox_header(MBX_ERROR,
											plat_get_string(IDS_2049),
											plat_get_string(IDS_4134));
						mvhd_close(vhd);
						return false;
					}
				}
				else
				{
					mvhd_close(vhd);
					return false;
				}
			}
			MVHDGeom vhd_geom = mvhd_get_geometry(vhd);
			adjust_vhd_geometry_for_86box(&vhd_geom);
			tracks = vhd_geom.cyl;
			hpc = vhd_geom.heads;
			spt = vhd_geom.spt;
			size = (uint64_t)tracks * hpc * spt * 512;
			mvhd_close(vhd);
        }
		else
		{
			fseeko64(f, 0, SEEK_END);
			size = ftello64(f);
			int i = 0;
			if (((size % 17) == 0) && (size <= 142606336))
			{
				spt = 17;
				if (size <= 26738688)
					hpc = 4;
				else if (((size % 3072) == 0) && (size <= 53477376))
					hpc = 6;
				else
				{
					for (i = 5; i < 16; i++)
					{
						if (((size % (i << 9)) == 0) &&
							(size <= ((i * 17) << 19)))
							break;
						if (i == 5)
							i++;
					}
					hpc = i;
				}
			}
			else
			{
				spt = 63;
				hpc = 16;
			}

            tracks = ((size >> 9) / hpc) / spt;
        }
		hdd_new->spt = spt;
		hdd_new->hpc = hpc;
		hdd_new->tracks = tracks;
		return true;
    }
	void RenderHDDCreationDialog()
	{
		if (ImGui::BeginPopupModal("Add Hard Disk", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::BeginDisabled(hdd_creation_ongoing);
			if (funcresult.valid() && hdd_creation_ongoing && funcresult.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready)
			{
				hdd_creation_ongoing = false;
				if (funcresult.get() == true) temp_hdd[CalcNextFreeHDD()] = *hdd_new;
				if (cur_img_type != 5)
				{
					ImGui::OpenPopup("Disk image created");
				}
				else ImGui::CloseCurrentPopup();
				funcresult = std::future<bool>();
			}
			ImGui::Text("File name:"); ImGui::SameLine();
			ImGui::InputText("##File name", hdd_new->fn, sizeof(hdd_new->fn));
			ImGui::SameLine();
			if (ImGui::Button("..."))
			{
				bool ok = OpenSettingsFileChooser(hdd_new->fn, sizeof(hdd_new->fn), "Hard disk images (*.HD? *.IM? *.VHD)|*.HD? *.IM? *.VHD", !hdd_existing);
				if (hdd_existing && ok)
				{
					if (!ParseHDD(hdd_new->fn))
					{
						hdd_new->fn[0] = '\0';
					}
				}
			}
			if ((cur_img_type != 5 && !hdd_existing) || (hdd_existing && hdd_new->fn[0] != 0))
			{
				ImGui::Text("Cylinder:"); ImGui::SameLine();
				ImGui::InputScalar("##Cylinder", ImGuiDataType_U32, &hdd_new->tracks);
				ImGui::Text("Heads:"); ImGui::SameLine();
				ImGui::InputScalar("##Heads", ImGuiDataType_U32, &hdd_new->hpc);
				ImGui::Text("Sector:"); ImGui::SameLine();
				ImGui::InputScalar("##Sector", ImGuiDataType_U32, &hdd_new->spt);
				if (hdd_new->tracks > max_tracks) hdd_new->tracks = max_tracks;
				if (hdd_new->hpc > max_hpc) hdd_new->hpc = max_hpc;
				if (hdd_new->spt > max_spt) hdd_new->spt = max_spt;
				uint32_t size_mb = (uint32_t)((((uint64_t)hdd_new->tracks * (uint64_t)hdd_new->hpc * (uint64_t)hdd_new->spt) << 9ULL) >> 20ULL);
				ImGui::TextUnformatted("Size (MB):"); ImGui::SameLine();
				if (ImGui::InputScalar("##Size (MB)", ImGuiDataType_U32, &size_mb))
				{
					hdd_image_calc_chs((uint32_t *) &hdd_new->tracks, (uint32_t *) &hdd_new->hpc, (uint32_t *) &hdd_new->spt, size_mb);
					if (hdd_new->tracks > max_tracks) hdd_new->tracks = max_tracks;
					if (hdd_new->hpc > max_hpc) hdd_new->hpc = max_hpc;
					if (hdd_new->spt > max_spt) hdd_new->spt = max_spt;
				}
				ImGui::TextUnformatted("Type"); ImGui::SameLine();
				if (ImGui::BeginCombo("##Type", hddtypes[getHddType(hdd_new->tracks, hdd_new->hpc, hdd_new->spt)].c_str()))
				{
					for (size_t i = 0; i < hddtypes.size(); i++)
					{
						if (ImGui::Selectable(hddtypes[i].c_str(), getHddType(hdd_new->tracks, hdd_new->hpc, hdd_new->spt) == i))
						{
							if (i < 127)
							{
								hdd_new->tracks = hdd_table[i][0];
								hdd_new->hpc = hdd_table[i][1];
								hdd_new->spt = hdd_table[i][2];
							}
							else
							{
								hdd_new->tracks = hdd_new->hpc = hdd_new->spt = 0;
								if (i == 128)
								{
									hdd_new->hpc = 16;
									hdd_new->spt = 63;
								}
							}
						}
					}
					ImGui::EndCombo();
				}
			}
			ImGui::TextUnformatted("Bus:");
			ImGui::SameLine();
			if (ImGui::BeginCombo("##Bus HDD Creation Dialog", busstr[hdd_new->bus].data()))
			{
				for (int i = 1; i < 7; i++)
				{
					if (ImGui::Selectable(busstr[i].data(), i == hdd_new->bus))
					{
						hdd_new->bus = i;
						switch(hdd_new->bus)
						{
							case HDD_BUS_DISABLED:
							default:
								max_spt = max_hpc = max_tracks = 0;
								break;
							case HDD_BUS_MFM:
								max_spt = 26;	/* 17 for MFM, 26 for RLL. */
								max_hpc = 15;
								max_tracks = 2047;
								break;
							case HDD_BUS_XTA:
								max_spt = 63;
								max_hpc = 16;
								max_tracks = 1023;
								break;
							case HDD_BUS_ESDI:
								max_spt = 99;	/* ESDI drives usually had 32 to 43 sectors per track. */
								max_hpc = 16;
								max_tracks = 266305;
								break;
							case HDD_BUS_IDE:
								max_spt = 63;
								max_hpc = 255;
								max_tracks = 266305;
								break;
							case HDD_BUS_ATAPI:
							case HDD_BUS_SCSI:
								max_spt = 99;
								max_hpc = 255;
								max_tracks = 266305;
								break;
						}
						DetermineNextFreeChannel(*hdd_new);
					}
					if (i == hdd_new->bus)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (!hdd_existing)
			{
				ImGui::TextUnformatted("Image type:"); ImGui::SameLine();
				if (ImGui::BeginCombo("##Image type", imgstr[cur_img_type].data()))
				{
					for (int i = 0; i < imgstr.size(); i++)
					{
						if (ImGui::Selectable(imgstr[i].data(), i == cur_img_type))
						{
							cur_img_type = i;
						}
						if (cur_img_type == i)
						{
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
			}
			ImGui::TextUnformatted("Channel:"); ImGui::SameLine();
			auto beginBinaryChannelCombo = [](uint8_t& chan, const char* str)
			{
				char chanstr[] = { '0', ':', (chan & 1) ? '1' : '0', 0 };
				if (ImGui::BeginCombo(str, chanstr))
				{
					for (int i = 0; i < 2; i++)
					{
						chanstr[2] = '0' + i;
						if (ImGui::Selectable(chanstr, chan == i))
						{
							chan = i;
						}
					}
					ImGui::EndCombo();
				}
			};
			
			switch(hdd_new->bus)
			{
				case HDD_BUS_IDE:
				case HDD_BUS_ATAPI:
				{
					if (ImGui::BeginCombo("##IDE Channel", (std::to_string(hdd_new->ide_channel >> 1) + ':' + std::to_string(hdd_new->ide_channel & 1)).c_str()))
					{
						for (int i = 0; i < 8; i++)
						{
							if (ImGui::Selectable((std::to_string(i >> 1) + ':' + std::to_string(i & 1)).c_str(), hdd_new->ide_channel == i))
							{
								hdd_new->ide_channel = i;
							}
						}
						ImGui::EndCombo();
					}
					break;
				}
				case HDD_BUS_MFM:
				{
					beginBinaryChannelCombo(hdd_new->mfm_channel, "##MFM/RLL");
					break;
				}
				case HDD_BUS_XTA:
				{
					beginBinaryChannelCombo(hdd_new->xta_channel, "##XTA");
					break;
				}
				case HDD_BUS_ESDI:
				{
					beginBinaryChannelCombo(hdd_new->xta_channel, "##ESDI");
					break;
				}
				case HDD_BUS_SCSI:
				{
					if (ImGui::BeginCombo("##SCSI ID", (std::to_string(hdd_new->scsi_id >> 4) + ':' + std::to_string(hdd_new->scsi_id & 15)).c_str()))
					{
						for (int i = 0; i < 64; i++)
						{
							if (ImGui::Selectable((std::to_string(i >> 4) + ':' + std::to_string(i & 15)).c_str(), hdd_new->scsi_id == i))
							{
								hdd_new->scsi_id = i;
							}
						}
						ImGui::EndCombo();
					}
					break;
				}
			}
			if (cur_img_type >= 4)
			{
				ImGui::TextUnformatted("Block size:"); ImGui::SameLine();
				if (ImGui::BeginCombo("##Block size", vhd_block_is_small ? "Small blocks (512 KB)" : "Large blocks (2 MB)"))
				{
					if (ImGui::Selectable("Small blocks (512 KB)", vhd_block_is_small)) vhd_block_is_small = true;
					if (ImGui::Selectable("Large blocks (2 MB)", !vhd_block_is_small)) vhd_block_is_small = false;
					ImGui::EndCombo();
				}
			}
			if (ImGui::Button("OK"))
			{
				if (hdd_new->fn[0] == '\0')
				{
					ui_msgbox_header(MBX_ERROR, (void*)L"Invalid configuration", (void*)L"Please specify a valid file name.");
				}
				else if (hdd_existing)
				{
					temp_hdd[CalcNextFreeHDD()] = *hdd_new;
					ImGui::CloseCurrentPopup();
				}
				else
				{
					if (cur_img_type == 0)
					{
						funcresult = std::async(std::launch::async, []() { return CreateHDD(hdd_new->fn, (hdd_new->tracks * hdd_new->hpc * hdd_new->spt) << 9LL); });
					}
					if (cur_img_type == 1)
					{
						HDDCreateHDI creationparams;
						creationparams.base = 0x1000;
						creationparams.size = (hdd_new->tracks * hdd_new->hpc * hdd_new->spt) << 9LL;
						creationparams.sector_size = 512;
						creationparams.tracks = hdd_new->tracks;
						creationparams.hpc = hdd_new->hpc;
						creationparams.spt = hdd_new->spt;
						creationparams.zero1 = creationparams.zero2 = 0;
						memset(&creationparams.pad, 0, sizeof(creationparams.pad));
						funcresult = std::async(std::launch::async, [creationparams]() { return CreateHDD(hdd_new->fn, creationparams); });
					}
					if (cur_img_type == 2)
					{
						HDDCreateHDX creationparams;
						creationparams.size = (hdd_new->tracks * hdd_new->hpc * hdd_new->spt) << 9LL;
						creationparams.sector_size = 512;
						creationparams.tracks = hdd_new->tracks;
						creationparams.hpc = hdd_new->hpc;
						creationparams.spt = hdd_new->spt;
						creationparams.zero1 = creationparams.zero2 = 0;
						funcresult = std::async(std::launch::async, [creationparams]() { return CreateHDD(hdd_new->fn, creationparams); });
					}
					if (cur_img_type >= 3)
					{
						HDDCreateVHD creationparams;
						creationparams.is_small_block = vhd_block_is_small;
						creationparams.dynamic = cur_img_type == 4;
						creationparams.differencing = cur_img_type == 5;
						if (cur_img_type == 5)
						{
							if (OpenSettingsFileChooser(creationparams.diff_file, sizeof(creationparams.diff_file), "VHD Files (*.VHD)|*.VHD", false))
							{
								funcresult = std::async(std::launch::async, [creationparams]() { return CreateHDD(hdd_new->fn, creationparams); });
							}
						}
						else
						{
							creationparams.hpc = hdd_new->hpc;
							creationparams.spt = hdd_new->spt;
							creationparams.tracks = hdd_new->tracks;
							funcresult = std::async(std::launch::async, [creationparams]() { return CreateHDD(hdd_new->fn, creationparams); });
						}
					}
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			if (hdd_creation_ongoing)
			{
				ImGui::ProgressBar((float)progress / (float)progress_max);
			}
			if (ImGui::BeginPopupModal("Disk image created"))
			{
				ImGui::Text("Remember to partition and format the newly-created drive.");
				if (ImGui::Button("OK"))
				{
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
				if (!ImGui::IsPopupOpen("Disk image created"))
				{
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
			if (!ImGui::IsPopupOpen("Add Hard Disk"))
			{
				delete hdd_new;
				hdd_new = nullptr;
			}
		}
	}

	void RenderHardDisksCategory() {
		normalize_hd_list();
		if (ImGui::BeginTable("##hddtable", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable, ImVec2(0, 110)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Bus");
			ImGui::TableSetupColumn("File");
			ImGui::TableSetupColumn("C");
			ImGui::TableSetupColumn("H");
			ImGui::TableSetupColumn("S");
			ImGui::TableSetupColumn("MB");
			ImGui::TableHeadersRow();
			for (int i = 0; i < HDD_NUM; i++)
			{
				static char hddname[512] = { 0 };
				std::fill(hddname, &hddname[sizeof(hddname)], 0);
				if (temp_hdd[i].bus <= HDD_BUS_DISABLED) continue;
				ImGui::TableNextRow();
				//if (cur_hdd_sel == i) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_Header));
				switch(temp_hdd[i].bus)
				{
					case HDD_BUS_IDE:
						snprintf(hddname, sizeof(hddname), "IDE (%01i:%01i)", temp_hdd[i].ide_channel >> 1, temp_hdd[i].ide_channel & 1);
						break;
					case HDD_BUS_ATAPI:
						snprintf(hddname, sizeof(hddname), "ATAPI (%01i:%01i)", temp_hdd[i].ide_channel >> 1, temp_hdd[i].ide_channel & 1);
						break;
					case HDD_BUS_ESDI:
						snprintf(hddname, sizeof(hddname), "ESDI (%01i:%01i)", temp_hdd[i].esdi_channel >> 1, temp_hdd[i].esdi_channel & 1);
						break;
					case HDD_BUS_MFM:
						snprintf(hddname, sizeof(hddname), "MFM/RLL (%01i:%01i)", temp_hdd[i].mfm_channel >> 1, temp_hdd[i].mfm_channel & 1);
						break;
					case HDD_BUS_XTA:
						snprintf(hddname, sizeof(hddname), "XTA (%01i:%01i)", temp_hdd[i].xta_channel >> 1, temp_hdd[i].xta_channel & 1);
						break;
					case HDD_BUS_SCSI:
						snprintf(hddname, sizeof(hddname), "SCSI (%01i:%02i)", temp_hdd[i].scsi_id >> 4, temp_hdd[i].scsi_id & 15);
						break;
				}
				ImGui::TableSetColumnIndex(0);
				if (ImGui::Selectable(hddname, cur_hdd_sel == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) cur_hdd_sel = i;;
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(!strnicmp(temp_hdd[i].fn, usr_path, strlen(usr_path)) ? temp_hdd[i].fn + strlen(usr_path) : temp_hdd[i].fn);

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%i", temp_hdd[i].tracks);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%i", temp_hdd[i].hpc);
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%i", temp_hdd[i].spt);
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%i", (temp_hdd[i].tracks * temp_hdd[i].hpc * temp_hdd[i].spt) >> 11);
			}
			ImGui::EndTable();
		}
		if (CalcNextFreeHDD() != -1)
		{
			if (ImGui::Button("Add..."))
			{
				OpenHDDCreationDialog();
			}
			ImGui::SameLine();
			if (ImGui::Button("Existing..."))
			{
				OpenHDDCreationDialog(true);
			}
		}
		if (CalcNextFreeHDD() != 0)
		{
			ImGui::SameLine();
			if (ImGui::Button("Remove"))
			{
				temp_hdd[cur_hdd_sel].fn[0] = '\0';
				hard_disk_untrack(cur_hdd_sel);
				temp_hdd[cur_hdd_sel].bus = HDD_BUS_DISABLED;
				// Normalization will happen next time this is rendered.
			}
		}
		if (temp_hdd[cur_hdd_sel].bus != HDD_BUS_DISABLED)
		{
			ImGui::TextUnformatted("Bus:"); ImGui::SameLine();
			if (ImGui::BeginCombo("##Bus:", busstr[temp_hdd[cur_hdd_sel].bus].data()))
			{
				for (int i = 1; i < 7; i++)
				{
					if (ImGui::Selectable(busstr[i].data(), i == temp_hdd[cur_hdd_sel].bus))
					{
						hard_disk_untrack(i);
						temp_hdd[cur_hdd_sel].bus = i;
						DetermineNextFreeChannel(temp_hdd[cur_hdd_sel]);
						hard_disk_track(i);
					}
					if (i == temp_hdd[cur_hdd_sel].bus)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
		
			ImGui::TextUnformatted("Channel:"); ImGui::SameLine();
			auto beginBinaryChannelCombo = [](uint8_t& chan, const char* str)
			{
				char chanstr[] = { '0', ':', (chan & 1) ? '1' : '0', 0 };
				if (ImGui::BeginCombo(str, chanstr))
				{
					for (int i = 0; i < 2; i++)
					{
						chanstr[2] = '0' + i;
						if (ImGui::Selectable(chanstr, chan == i))
						{
							chan = i;
						}
					}
					ImGui::EndCombo();
				}
			};
			
			switch(temp_hdd[cur_hdd_sel].bus)
			{
				case HDD_BUS_IDE:
				case HDD_BUS_ATAPI:
				{
					if (ImGui::BeginCombo("##IDE Channel", (std::to_string(temp_hdd[cur_hdd_sel].ide_channel >> 1) + ':' + std::to_string(temp_hdd[cur_hdd_sel].ide_channel & 1)).c_str()))
					{
						for (int i = 0; i < 8; i++)
						{
							if (ImGui::Selectable((std::to_string(i >> 1) + ':' + std::to_string(i & 1)).c_str(), temp_hdd[cur_hdd_sel].ide_channel == i))
							{
								temp_hdd[cur_hdd_sel].ide_channel = i;
							}
						}
						ImGui::EndCombo();
					}
					break;
				}
				case HDD_BUS_MFM:
				{
					beginBinaryChannelCombo(temp_hdd[cur_hdd_sel].mfm_channel, "##MFM/RLL");
					break;
				}
				case HDD_BUS_XTA:
				{
					beginBinaryChannelCombo(temp_hdd[cur_hdd_sel].xta_channel, "##XTA");
					break;
				}
				case HDD_BUS_ESDI:
				{
					beginBinaryChannelCombo(temp_hdd[cur_hdd_sel].xta_channel, "##ESDI");
					break;
				}
				case HDD_BUS_SCSI:
				{
					if (ImGui::BeginCombo("##SCSI ID", (std::to_string(temp_hdd[cur_hdd_sel].scsi_id >> 4) + ':' + std::to_string(temp_hdd[cur_hdd_sel].scsi_id & 15)).c_str()))
					{
						for (int i = 0; i < 64; i++)
						{
							if (ImGui::Selectable((std::to_string(i >> 4) + ':' + std::to_string(i & 15)).c_str(), temp_hdd[cur_hdd_sel].scsi_id == i))
							{
								temp_hdd[cur_hdd_sel].scsi_id = i;
							}
						}
						ImGui::EndCombo();
					}
					break;
				}
			}
		}
		RenderHDDCreationDialog();
	}
	static int cur_fdd_sel = 0;
	static int cur_cd_sel = 0;
	void RenderFloppyCdromDrivesCategory()
	{
		if (ImGui::BeginTable("##fddtable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable, ImVec2(0, 110)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Type");
			ImGui::TableSetupColumn("Turbo");
			ImGui::TableSetupColumn("Check BPB");
			ImGui::TableHeadersRow();
			for (int i = 0; i < FDD_NUM; i++)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				if (ImGui::Selectable((std::string(fdd_getname(temp_fdd_types[i])) + "##" + std::to_string(i)).c_str(), i == cur_fdd_sel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap))
				{
					cur_fdd_sel = i;
				}
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(temp_fdd_turbo[i] ? "On" : "Off");
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(temp_fdd_check_bpb[i] ? "On" : "Off");
			}
			ImGui::EndTable();
		}
		ImGui::TextUnformatted("Type:"); ImGui::SameLine();
		if (ImGui::BeginCombo("##Type FDD", fdd_getname(temp_fdd_types[cur_fdd_sel])))
		{
			int c = 0;
			char* fddtypename = nullptr;
			while ((fddtypename = fdd_getname(c))[0] != 0)
			{
				if (ImGui::Selectable(fddtypename, temp_fdd_types[cur_fdd_sel] == c))
				{
					temp_fdd_types[cur_fdd_sel] = c;
				}
				if (temp_fdd_types[cur_fdd_sel] == c)
				{
					ImGui::SetItemDefaultFocus();
				}
				c++;
			}
			ImGui::EndCombo();
		}
		ImGui::CheckboxFlags("Turbo", &temp_fdd_turbo[cur_fdd_sel], 1); ImGui::SameLine();
		ImGui::CheckboxFlags("Check BPB", &temp_fdd_check_bpb[cur_fdd_sel], 1);

		if (ImGui::BeginTable("##cdtable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable, ImVec2(0, 110)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Type");
			ImGui::TableSetupColumn("Speed");
			ImGui::TableHeadersRow();
			for (int i = 0; i < 4; i++)
			{
				static char hddname[512] = { 0 };
				std::fill(hddname, &hddname[sizeof(hddname)], 0);
				ImGui::TableNextRow();
				switch(temp_cdrom[i].bus_type)
				{
					default:
					case CDROM_BUS_DISABLED:
						strncpy(hddname, "Disabled", sizeof("Disabled"));
						break;
					case CDROM_BUS_ATAPI:
						snprintf(hddname, sizeof(hddname), "ATAPI (%01i:%01i)", temp_cdrom[i].ide_channel >> 1, temp_cdrom[i].ide_channel & 1);
						break;
					case CDROM_BUS_SCSI:
						snprintf(hddname, sizeof(hddname), "SCSI (%01i:%02i)", temp_cdrom[i].scsi_device_id >> 4, temp_cdrom[i].scsi_device_id & 15);
						break;
				}
				strcat(hddname, ("##" + std::to_string(i)).c_str());
				ImGui::TableSetColumnIndex(0);
				if (ImGui::Selectable(hddname, cur_cd_sel == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) cur_cd_sel = i;
				ImGui::TableNextColumn();
				if (temp_cdrom[i].bus_type) ImGui::Text("%ix", temp_cdrom[i].speed);
				else ImGui::TextUnformatted("None");
			}
			ImGui::EndTable();
		}
		ImGui::TextUnformatted("Bus:"); ImGui::SameLine();
		if (ImGui::BeginCombo("##Bus CDROM", busstr[temp_cdrom[cur_cd_sel].bus_type].data()))
		{
			auto cur_bus_type = temp_cdrom[cur_cd_sel].bus_type;
			cdrom_untrack(cur_cd_sel);
			if (ImGui::Selectable(busstr[0].data(), temp_cdrom[cur_cd_sel].bus_type == 0)) temp_cdrom[cur_cd_sel].bus_type = 0;
			else if (ImGui::Selectable(busstr[CDROM_BUS_ATAPI].data(), temp_cdrom[cur_cd_sel].bus_type == CDROM_BUS_ATAPI)) temp_cdrom[cur_cd_sel].bus_type = CDROM_BUS_ATAPI;
			else if (ImGui::Selectable(busstr[CDROM_BUS_SCSI].data(), temp_cdrom[cur_cd_sel].bus_type == CDROM_BUS_SCSI)) temp_cdrom[cur_cd_sel].bus_type = CDROM_BUS_SCSI;
			ImGui::EndCombo();
			if (temp_cdrom[cur_cd_sel].bus_type != 0 && temp_cdrom[cur_cd_sel].speed < 1) temp_cdrom[cur_cd_sel].speed = 1;
			if (temp_cdrom[cur_cd_sel].bus_type)
			{
				if (cur_bus_type != temp_cdrom[cur_cd_sel].bus_type) DetermineNextFreeChannel(temp_cdrom[cur_cd_sel]);
				cdrom_track(cur_cd_sel);
			}
		}
		if (temp_cdrom[cur_cd_sel].bus_type)
		{
			ImGui::TextUnformatted("Speed:"); ImGui::SameLine();
			
			if (ImGui::BeginCombo("##Speed CDROM", (std::to_string(temp_cdrom[cur_cd_sel].speed) + 'x').c_str()))
			{
				for (int i = 1; i <= 72; i++)
				{
					if (ImGui::Selectable((std::to_string(i) + 'x').c_str(), i == temp_cdrom[cur_cd_sel].speed)) temp_cdrom[cur_cd_sel].speed = i;
				}
				ImGui::EndCombo();
			}
			ImGui::TextUnformatted("Channel:"); ImGui::SameLine();
			switch (temp_cdrom[cur_cd_sel].bus_type)
			{
				case CDROM_BUS_ATAPI:
				{
					if (ImGui::BeginCombo("##IDE Channel CD", (std::to_string(temp_cdrom[cur_cd_sel].ide_channel >> 1) + ':' + std::to_string(temp_cdrom[cur_cd_sel].ide_channel & 1)).c_str()))
					{
						for (int i = 0; i < 8; i++)
						{
							if (ImGui::Selectable((std::to_string(i >> 1) + ':' + std::to_string(i & 1)).c_str(), temp_cdrom[cur_cd_sel].ide_channel == i))
							{
								temp_cdrom[cur_cd_sel].ide_channel = i;
							}
						}
						ImGui::EndCombo();
					}
					break;
				}
				case CDROM_BUS_SCSI:
				{
					if (ImGui::BeginCombo("##SCSI ID CD", (std::to_string(temp_cdrom[cur_cd_sel].scsi_device_id >> 4) + ':' + std::to_string(temp_cdrom[cur_cd_sel].scsi_device_id & 15)).c_str()))
					{
						for (int i = 0; i < 64; i++)
						{
							if (ImGui::Selectable((std::to_string(i >> 4) + ':' + std::to_string(i & 15)).c_str(), temp_cdrom[cur_cd_sel].scsi_device_id == i))
							{
								temp_cdrom[cur_cd_sel].scsi_device_id = i;
							}
						}
						ImGui::EndCombo();
					}
					break;
				}
			}
		}
	}
	static int cur_zip_sel = 0, cur_mo_sel = 0;
	void RenderOtherRemovableDevicesCategory()
	{
		if (ImGui::BeginTable("##motable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable, ImVec2(0, 110)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Bus");
			ImGui::TableSetupColumn("Type");
			ImGui::TableHeadersRow();
			for (int i = 0; i < MO_NUM; i++)
			{
				static char hddname[512] = { 0 };
				std::fill(hddname, &hddname[sizeof(hddname)], 0);
				ImGui::TableNextRow();
				switch(temp_mo_drives[i].bus_type)
				{
					default:
					case MO_BUS_DISABLED:
						strncpy(hddname, "Disabled", sizeof("Disabled"));
						break;
					case MO_BUS_ATAPI:
						snprintf(hddname, sizeof(hddname), "ATAPI (%01i:%01i)", temp_mo_drives[i].ide_channel >> 1, temp_mo_drives[i].ide_channel & 1);
						break;
					case MO_BUS_SCSI:
						snprintf(hddname, sizeof(hddname), "SCSI (%01i:%02i)", temp_mo_drives[i].scsi_device_id >> 4, temp_mo_drives[i].scsi_device_id & 15);
						break;
				}
				strcat(hddname, ("##" + std::to_string(i) + "MO").c_str());
				ImGui::TableSetColumnIndex(0);
				if (ImGui::Selectable(hddname, cur_mo_sel == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) cur_mo_sel = i;
				ImGui::TableNextColumn();
				if (!temp_mo_drives[i].bus_type) ImGui::TextUnformatted("None");
				else ImGui::Text("%.8s %.16s %.4s", mo_drive_types[temp_mo_drives[i].type].vendor, mo_drive_types[temp_mo_drives[i].type].model, mo_drive_types[temp_mo_drives[i].type].revision);
			}
			ImGui::EndTable();
		}
		ImGui::TextUnformatted("Bus:"); ImGui::SameLine();
		if (ImGui::BeginCombo("##Bus MO", busstr[temp_mo_drives[cur_mo_sel].bus_type].data()))
		{
			auto cur_bus_type = temp_mo_drives[cur_mo_sel].bus_type;
			mo_untrack(cur_mo_sel);
			if (ImGui::Selectable(busstr[0].data(), temp_mo_drives[cur_mo_sel].bus_type == 0)) temp_mo_drives[cur_mo_sel].bus_type = 0;
			else if (ImGui::Selectable(busstr[MO_BUS_ATAPI].data(), temp_mo_drives[cur_mo_sel].bus_type == MO_BUS_ATAPI)) temp_mo_drives[cur_mo_sel].bus_type = MO_BUS_ATAPI;
			else if (ImGui::Selectable(busstr[MO_BUS_SCSI].data(), temp_mo_drives[cur_mo_sel].bus_type == MO_BUS_SCSI)) temp_mo_drives[cur_mo_sel].bus_type = MO_BUS_SCSI;
			ImGui::EndCombo();
			if (temp_mo_drives[cur_mo_sel].bus_type)
			{
				if (cur_bus_type != temp_mo_drives[cur_mo_sel].bus_type) DetermineNextFreeChannel(temp_mo_drives[cur_mo_sel]);
				mo_track(cur_mo_sel);
			}
		}
		if (temp_mo_drives[cur_mo_sel].bus_type)
		{
			char motypenamecur[128] = { 0 };
			ImGui::SameLine(); ImGui::TextUnformatted("Channel:"); ImGui::SameLine();
			switch (temp_mo_drives[cur_mo_sel].bus_type)
			{
				case MO_BUS_ATAPI:
				{
					if (ImGui::BeginCombo("##IDE Channel MO", (std::to_string(temp_mo_drives[cur_mo_sel].ide_channel >> 1) + ':' + std::to_string(temp_mo_drives[cur_mo_sel].ide_channel & 1)).c_str()))
					{
						for (int i = 0; i < 8; i++)
						{
							if (ImGui::Selectable((std::to_string(i >> 1) + ':' + std::to_string(i & 1)).c_str(), temp_mo_drives[cur_mo_sel].ide_channel == i))
							{
								temp_mo_drives[cur_mo_sel].ide_channel = i;
							}
						}
						ImGui::EndCombo();
					}
					break;
				}
				case MO_BUS_SCSI:
				{
					if (ImGui::BeginCombo("##SCSI ID MO", (std::to_string(temp_mo_drives[cur_mo_sel].scsi_device_id >> 4) + ':' + std::to_string(temp_mo_drives[cur_mo_sel].scsi_device_id & 15)).c_str()))
					{
						for (int i = 0; i < 64; i++)
						{
							if (ImGui::Selectable((std::to_string(i >> 4) + ':' + std::to_string(i & 15)).c_str(), temp_mo_drives[cur_mo_sel].scsi_device_id == i))
							{
								temp_mo_drives[cur_mo_sel].scsi_device_id = i;
							}
						}
						ImGui::EndCombo();
					}
					break;
				}
			}
			ImGui::TextUnformatted("Type:"); ImGui::SameLine();
			snprintf(motypenamecur, sizeof(motypenamecur), "%.8s %.16s %.4s", mo_drive_types[temp_mo_drives[cur_mo_sel].type].vendor, mo_drive_types[temp_mo_drives[cur_mo_sel].type].model, mo_drive_types[temp_mo_drives[cur_mo_sel].type].revision);
			if (ImGui::BeginCombo("Type##MO", motypenamecur))
			{
				for (int i = 0; i < KNOWN_MO_DRIVE_TYPES; i++)
				{
					char motypename[128] = { 0 };
					snprintf(motypename, sizeof(motypename), "%.8s %.16s %.4s", mo_drive_types[i].vendor, mo_drive_types[i].model, mo_drive_types[i].revision);
					if (ImGui::Selectable(motypename, temp_mo_drives[cur_mo_sel].type == i))
					{
						temp_mo_drives[cur_mo_sel].type = i;
					}
					if (temp_mo_drives[cur_mo_sel].type == i)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
		}

		if (ImGui::BeginTable("##ziptable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable, ImVec2(0, 110)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Bus");
			ImGui::TableSetupColumn("Type");
			ImGui::TableHeadersRow();
			for (int i = 0; i < ZIP_NUM; i++)
			{
				static char hddname[512] = { 0 };
				std::fill(hddname, &hddname[sizeof(hddname)], 0);
				ImGui::TableNextRow();
				switch(temp_zip_drives[i].bus_type)
				{
					default:
					case ZIP_BUS_DISABLED:
						strncpy(hddname, "Disabled", sizeof("Disabled"));
						break;
					case ZIP_BUS_ATAPI:
						snprintf(hddname, sizeof(hddname), "ATAPI (%01i:%01i)", temp_zip_drives[i].ide_channel >> 1, temp_zip_drives[i].ide_channel & 1);
						break;
					case ZIP_BUS_SCSI:
						snprintf(hddname, sizeof(hddname), "SCSI (%01i:%02i)", temp_zip_drives[i].scsi_device_id >> 4, temp_zip_drives[i].scsi_device_id & 15);
						break;
				}
				strcat(hddname, ("##" + std::to_string(i)).c_str());
				ImGui::TableSetColumnIndex(0);
				if (ImGui::Selectable(hddname, cur_zip_sel == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) cur_zip_sel = i;
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(temp_zip_drives[i].is_250 ? "ZIP 250" : "ZIP 100");
			}
			ImGui::EndTable();
		}
		ImGui::TextUnformatted("Bus:"); ImGui::SameLine();
		if (ImGui::BeginCombo("##Bus ZIP", busstr[temp_zip_drives[cur_zip_sel].bus_type].data()))
		{
			auto cur_bus_type = temp_zip_drives[cur_zip_sel].bus_type;
			zip_untrack(cur_zip_sel);
			if (ImGui::Selectable(busstr[0].data(), temp_zip_drives[cur_zip_sel].bus_type == 0)) temp_zip_drives[cur_zip_sel].bus_type = 0;
			else if (ImGui::Selectable(busstr[ZIP_BUS_ATAPI].data(), temp_zip_drives[cur_zip_sel].bus_type == ZIP_BUS_ATAPI)) temp_zip_drives[cur_zip_sel].bus_type = ZIP_BUS_ATAPI;
			else if (ImGui::Selectable(busstr[ZIP_BUS_SCSI].data(), temp_zip_drives[cur_zip_sel].bus_type == ZIP_BUS_SCSI)) temp_zip_drives[cur_zip_sel].bus_type = ZIP_BUS_SCSI;
			ImGui::EndCombo();
			if (temp_zip_drives[cur_zip_sel].bus_type)
			{
				if (cur_bus_type != temp_zip_drives[cur_zip_sel].bus_type) DetermineNextFreeChannel(temp_zip_drives[cur_zip_sel]);
				zip_track(cur_zip_sel);
			}
		}
		ImGui::SameLine();
		ImGui::TextUnformatted("Channel:"); ImGui::SameLine();
		switch (temp_zip_drives[cur_zip_sel].bus_type)
		{
			case ZIP_BUS_ATAPI:
			{
				if (ImGui::BeginCombo("##IDE Channel ZIP", (std::to_string(temp_zip_drives[cur_zip_sel].ide_channel >> 1) + ':' + std::to_string(temp_zip_drives[cur_zip_sel].ide_channel & 1)).c_str()))
				{
					for (int i = 0; i < 8; i++)
					{
						if (ImGui::Selectable((std::to_string(i >> 1) + ':' + std::to_string(i & 1)).c_str(), temp_zip_drives[cur_zip_sel].ide_channel == i))
						{
							temp_zip_drives[cur_zip_sel].ide_channel = i;
						}
					}
					ImGui::EndCombo();
				}
				break;
			}
			case ZIP_BUS_SCSI:
			{
				if (ImGui::BeginCombo("##SCSI ID ZIP", (std::to_string(temp_zip_drives[cur_zip_sel].scsi_device_id >> 4) + ':' + std::to_string(temp_zip_drives[cur_zip_sel].scsi_device_id & 15)).c_str()))
				{
					for (int i = 0; i < 64; i++)
					{
						if (ImGui::Selectable((std::to_string(i >> 4) + ':' + std::to_string(i & 15)).c_str(), temp_zip_drives[cur_zip_sel].scsi_device_id == i))
						{
							temp_zip_drives[cur_zip_sel].scsi_device_id = i;
						}
					}
					ImGui::EndCombo();
				}
				break;
			}
		}
		ImGui::CheckboxFlags("ZIP 250", &temp_zip_drives[cur_zip_sel].is_250, 1);
	}

	void RenderOtherPeripheralsCategory() {
		RenderDeviceCombo("ISA RTC:", isartc_get_device, isartc_get_internal_name, [](int) { return true; }, temp_isartc, 0, 0, true, [](int c) { return !!(isartc_get_device(c) && isartc_get_device(c)->config); });
		ImGui::Text("ISA Memory Expansion");
		for (int i = 0; i < ISAMEM_MAX; i++)
		{
			RenderDeviceCombo((std::string("Card ") + std::to_string(i + 1) + ':').c_str(), isamem_get_device, isamem_get_internal_name, [](int) { return true; }, temp_isamem[i], 0, 0, true, [](int c) { return c != 0; }, i + 1);
		}
		ImGui::CheckboxFlags("ISABugger device", &temp_bugger, 1);
		ImGui::CheckboxFlags("POST card", &temp_postcard, 1);
	}

} // namespace ImGuiSettingsWindow

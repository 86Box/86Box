#include <array>
#include <vector>
#include <iostream>
#include <string>
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
	static uint64_t mfm_tracking, esdi_tracking, xta_tracking, ide_tracking, scsi_tracking[8];
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
	void RenderSoundCategory();
	void RenderNetworkCategory();
	void RenderPortsCategory();
	void RenderStorageControllersCategory();
	void RenderHardDisksCategory();
	void RenderFloppyCdromDrivesCategory();
	void RenderOtherRemovableDevicesCategory();
	void RenderOtherPeripheralsCategory();

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
		for (i = 0; i < 8; i++)
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
			scsi_tracking[hdd[i].scsi_id >> 3] |= (1 << ((hdd[i].scsi_id & 0x07) << 3));
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
			scsi_tracking[cdrom[i].scsi_device_id >> 3] |= (1 << ((cdrom[i].scsi_device_id & 0x07) << 3));
		}
		memcpy(temp_zip_drives, zip_drives, ZIP_NUM * sizeof(zip_drive_t));
		for (i = 0; i < ZIP_NUM; i++) {
		if (zip_drives[i].bus_type == ZIP_BUS_ATAPI)
			ide_tracking |= (4 << (zip_drives[i].ide_channel << 3));
		else if (zip_drives[i].bus_type == ZIP_BUS_SCSI)
			scsi_tracking[zip_drives[i].scsi_device_id >> 3] |= (1 << ((zip_drives[i].scsi_device_id & 0x07) << 3));
		}
		memcpy(temp_mo_drives, mo_drives, MO_NUM * sizeof(mo_drive_t));
		for (i = 0; i < MO_NUM; i++) {
		if (mo_drives[i].bus_type == MO_BUS_ATAPI)
		ide_tracking |= (1 << (mo_drives[i].ide_channel << 3));
		else if (mo_drives[i].bus_type == MO_BUS_SCSI)
		scsi_tracking[mo_drives[i].scsi_device_id >> 3] |= (1 << ((mo_drives[i].scsi_device_id & 0x07) << 3));
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
	void SaveSettings();
	void Render() {
		//ImGui::Begin("Settings", &ImGuiSettingsWindow::showSettingsWindow);
		if (!ImGui::BeginPopupModal("Settings Window", &showSettingsWindow)) return;

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
			if(ImGui::Selectable("Sound")) {
				currentSettingsCategory = 2;
			} else
			if(ImGui::Selectable("Network")) {
				currentSettingsCategory = 3;
			} else
			if(ImGui::Selectable("Ports (COM & LPT)")) {
				currentSettingsCategory = 4;
			} else
			if(ImGui::Selectable("Storage Controllers")) {
				currentSettingsCategory = 5;
			} else
			if(ImGui::Selectable("Hard Disks")) {
				currentSettingsCategory = 6;
			} else
			if(ImGui::Selectable("Floppy & CD-ROM Drives")) {
				currentSettingsCategory = 7;
			} else
			if(ImGui::Selectable("Other Removable Devices")) {
				currentSettingsCategory = 8;
			} else
			if(ImGui::Selectable("Other Peripherals")) {
				currentSettingsCategory = 9;
			}

			ImGui::EndChild();
		}
		ImGui::SameLine();

		// Right
		{
			ImGui::BeginGroup();
			ImGui::BeginChild("item view", ImVec2(0, -ImGui::GetFrameHeightWithSpacing())); // Leave room for 1 line below us

			ImGui::Separator();

			switch(currentSettingsCategory) {
				case 0: RenderMachineCategory(); break;
				case 1: RenderDisplayCategory(); break;
				case 2: RenderSoundCategory(); break;
				case 3: RenderNetworkCategory(); break;
				case 4: RenderPortsCategory(); break;
				case 5: RenderStorageControllersCategory(); break;
				case 6: RenderHardDisksCategory(); break;
				case 7: RenderFloppyCdromDrivesCategory(); break;
				case 8: RenderOtherRemovableDevicesCategory(); break;
				case 9: RenderOtherPeripheralsCategory(); break;
				default: RenderMachineCategory();
			}

			ImGui::EndChild();
			if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
			ImGui::SameLine();
			if (ImGui::Button("Save")) { SaveSettings(); ImGui::CloseCurrentPopup(); }
			ImGui::EndGroup();
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
				while (temp_cpu_f->cpus[c].cpu_type != 0)
				{
					if (cpu_is_eligible(temp_cpu_f, c, temp_machine))
					{
						temp_cpu = c;
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
		const std::array cpu_types {"1", "2", "3", "4"};
		static int cpu_current = 0;
		const char* cpu_preview_value = cpu_types[cpu_current];  // Pass in the preview value visible before opening the combo (it could be anything)
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
		ImGui::Text("FPU:");
		ImGui::SameLine();
		auto getFPUIndex = [&]()
		{
			int c = 0;
			size_t i = 0;
			while (1)
			{
				if (temp_cpu_f->cpus[temp_cpu].fpus[c].type == temp_fpu)
				{
					i = c;
					break;
				}
				if (temp_cpu_f->cpus[temp_cpu].fpus[c].name == NULL) break;
				c++;
			}
			if (i == 0) temp_fpu = temp_cpu_f->cpus[temp_cpu].fpus[0].type;
			return i;
		};
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
		static int memory_amount_mb = 0;
		

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
			temp_mem_size = memory_amount_mb * selected_machine.ram_granularity;
			while (temp_mem_size > selected_machine.max_ram) {
				temp_mem_size -= selected_machine.ram_granularity;
			}
			if (temp_mem_size < selected_machine.min_ram) {
				temp_mem_size = selected_machine.min_ram;
			}
			memory_amount_mb = temp_mem_size / selected_machine.ram_granularity;
		}
		else {
			ImGui::Text("Memory (KB):");
			ImGui::SameLine();
			ImGui::InputInt("##memory", (int*)&temp_mem_size, selected_machine.ram_granularity, selected_machine.ram_granularity, ImGuiInputTextFlags_EnterReturnsTrue);
			while (temp_mem_size > selected_machine.max_ram) {
				temp_mem_size -= selected_machine.ram_granularity;
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


	void RenderDisplayCategory() {

	}

	void RenderSoundCategory() {

	}

	void RenderNetworkCategory() {

	}

	void RenderPortsCategory() {

	}

	void RenderStorageControllersCategory() {

	}

	void RenderHardDisksCategory() {

	}

	void RenderFloppyCdromDrivesCategory() {

	}

	void RenderOtherRemovableDevicesCategory() {

	}

	void RenderOtherPeripheralsCategory() {

	}

} // namespace ImGuiSettingsWindow

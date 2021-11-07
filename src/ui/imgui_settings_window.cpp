#include <array>
#include <vector>
#include <iostream>
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
	static int temp_dynarec;
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
			if (ImGui::Button("Save")) {}
			ImGui::EndGroup();
		}

		ImGui::EndPopup();
	}

	void RenderMachineCategory() {

		//ImGui::PushItemWidth(ImGui::GetFontSize() * -12);
		//ImGui::AlignTextToFramePadding();


		std::vector<char *> item_list; // will be reused for memory savings

		//////////////////////////////
		// Machine Type Combo Drop Down Box
		//////////////////////////////
		for (std::size_t i = 0; i < machine_type_count(); ++i) {
			item_list.push_back(machine_type_getname(i));
		}
		static int machine_type_current = machines[machine].type;;
		const char* machine_type_preview_value = item_list[machine_type_current];
		ImGui::Text("Machine Type:");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##Machine Type", machine_type_preview_value))
		{
			for (int n = 0; n < item_list.size(); n++)
			{
				const bool is_selected = (machine_type_current == n);
				if (ImGui::Selectable(item_list.at(n), is_selected))
					machine_type_current = n;

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
		item_list.clear();
		for (std::size_t i = 0; i < machine_count(); ++i) {
			if (machine_available(i) && machine_get_type_from_id(i) == machine_type_current) {
				item_list.push_back(machine_getname_from_id(i));
			}
		}
		static int machine_current = 0;
		const char* machine_preview_value = item_list.at(machine_current);  // Pass in the preview value visible before opening the combo (it could be anything)
		ImGui::Text("Machine:");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##Machine", machine_preview_value))
		{
			for (int n = 0; n < item_list.size(); n++)
			{
				const bool is_selected = (machine_current == n);
				if (ImGui::Selectable(item_list.at(n), is_selected))
					machine_current = n;

				// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		//char* machine_name = item_list.size() > 0 ? item_list.at(machine_current) : "";
		machine_t selected_machine = machine_get_from_id(machine_current);

		//////////////////////////////
		// CPU Type Combo Drop Down
		//////////////////////////////
		const std::array cpu_types {"1", "2", "3", "4"};
		static int cpu_current = 0;
		const char* cpu_preview_value = cpu_types[cpu_current];  // Pass in the preview value visible before opening the combo (it could be anything)
		ImGui::Text("CPU:");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##CPU Type", cpu_preview_value))
		{
			for (int n = 0; n < cpu_types.size(); n++)
			{
				const bool is_selected = (cpu_current == n);
				if (ImGui::Selectable(cpu_types[n], is_selected))
					cpu_current = n;

				// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		//////////////////////////////
		// CPU Speed Combo Drop Down
		//////////////////////////////
		const std::array cpu_speed_types {"1", "2", "3", "4"};
		static int cpu_speed_current = 0;
		const char* cpu_speed_preview_value = cpu_speed_types[cpu_current];  // Pass in the preview value visible before opening the combo (it could be anything)
		ImGui::Text("Speed:");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##Speed", cpu_speed_preview_value))
		{
			for (int n = 0; n < cpu_speed_types.size(); n++)
			{
				const bool is_selected = (cpu_speed_current == n);
				if (ImGui::Selectable(cpu_speed_types[n], is_selected))
					cpu_speed_current = n;

				// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		//////////////////////////////
		// FPU Combo Drop Down
		//////////////////////////////
		const std::array fpu_types {"1", "2", "3", "4"};
		static int fpu_current = 0;
		const char* fpu_preview_value = fpu_types[fpu_current];  // Pass in the preview value visible before opening the combo (it could be anything)
		ImGui::Text("FPU:");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##FPU", fpu_preview_value))
		{
			for (int n = 0; n < fpu_types.size(); n++)
			{
				const bool is_selected = (cpu_speed_current == n);
				if (ImGui::Selectable(fpu_types[n], is_selected))
					fpu_current = n;

				// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		//////////////////////////////
		// Wait States Combo Drop Down
		//////////////////////////////
		const std::array wait_states_types {"1", "2", "3", "4"};
		static int wait_state_current = 0;
		const char* wait_state_preview_value = wait_states_types[wait_state_current];  // Pass in the preview value visible before opening the combo (it could be anything)
		ImGui::Text("Wait States:");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##WaitStates", wait_state_preview_value))
		{
			for (int n = 0; n < wait_states_types.size(); n++)
			{
				const bool is_selected = (wait_state_current == n);
				if (ImGui::Selectable(wait_states_types[n], is_selected))
					wait_state_current = n;

				// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		//////////////////////////////
		// RAM/Memory Config
		//////////////////////////////
		static int memory_amount = 0;

		// if (memory_amount > selected_machine.max_ram) {
		// 	memory_amount = selected_machine.max_ram;
		// }

		std::cout << "Ram Granularity = " << selected_machine.ram_granularity << "\n";
		std::cout << "Machine: " << selected_machine.name << "\n";

		// ram_granularity is in kilobytes, if it's more than 1024 then step in megabytes
		if (selected_machine.ram_granularity >= 1024) {
			ImGui::Text("Memory (MB):");
			ImGui::SameLine();
			ImGui::InputInt("##memory", &memory_amount, selected_machine.ram_granularity/1024, selected_machine.ram_granularity/1024, ImGuiInputTextFlags_EnterReturnsTrue);
			while (memory_amount > selected_machine.max_ram) {
				memory_amount -= selected_machine.ram_granularity/1024;
			}
			if (memory_amount < selected_machine.min_ram) {
				memory_amount == selected_machine.min_ram;
			}
		}
		else {
			ImGui::Text("Memory (KB):");
			ImGui::SameLine();
			ImGui::InputInt("##memory", &memory_amount, selected_machine.ram_granularity, selected_machine.ram_granularity, ImGuiInputTextFlags_EnterReturnsTrue);
			while (memory_amount > selected_machine.max_ram) {
				memory_amount -= selected_machine.ram_granularity;
			}
			if (memory_amount < selected_machine.min_ram) {
				memory_amount == selected_machine.min_ram;
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
		static bool isUsingDynarec = false;
		ImGui::Text("Dynamic Recompiler");
		ImGui::SameLine();
		ImGui::Checkbox("##Dynarec", &isUsingDynarec);

		//////////////////////////////
		// Time Syncronization Radio Selection
		//////////////////////////////

		ImGui::Text("Time Syncronization");
		static int timeSyncMode = 0;
		if (ImGui::RadioButton("Disabled", timeSyncMode == TIME_SYNC_DISABLED)) {
			timeSyncMode = TIME_SYNC_DISABLED;
		}
		if (ImGui::RadioButton("Enabled (local time)", timeSyncMode == TIME_SYNC_ENABLED)) {
			timeSyncMode = TIME_SYNC_ENABLED;
		}
		if (ImGui::RadioButton("Enabled (UTC)", timeSyncMode == TIME_SYNC_UTC)) {
			timeSyncMode = TIME_SYNC_UTC;
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

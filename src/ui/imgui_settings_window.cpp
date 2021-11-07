#include <array>
#include <vector>
#include <iostream>
#include "imgui.h"
#include <86box/imgui_settings_window.h>

extern "C" {
#include <86box/machine.h>
}

namespace ImGuiSettingsWindow {

	bool showSettingsWindow = false;

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
		enum TimeSyncMode
		{
			TIME_SYNC_DISABLED,
			TIME_SYNC_LOCAL,
			TIME_SYNC_UTC
		};

		ImGui::Text("Time Syncronization");
		static int timeSyncMode = 0;
		if (ImGui::RadioButton("Disabled", timeSyncMode == TimeSyncMode::TIME_SYNC_DISABLED)) {
			timeSyncMode = TimeSyncMode::TIME_SYNC_DISABLED;
		}
		if (ImGui::RadioButton("Enabled (local time)", timeSyncMode == TimeSyncMode::TIME_SYNC_LOCAL)) {
			timeSyncMode = TimeSyncMode::TIME_SYNC_LOCAL;
		}
		if (ImGui::RadioButton("Enabled (UTC)", timeSyncMode == TimeSyncMode::TIME_SYNC_UTC)) {
			timeSyncMode = TimeSyncMode::TIME_SYNC_UTC;
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

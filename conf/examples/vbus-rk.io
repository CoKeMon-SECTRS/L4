-- vim:ft=lua

local hw = Io.system_bus()

Io.add_vbusses({
	vbus_emmc0 = Io.Vi.System_bus(function()
		mmc0 = wrap(hw.mmc0)
	end),
	vbus_emmc1 = Io.Vi.System_bus(function()
		mmc1 = wrap(hw.mmc1)
	end),
	vbus_emmc2 = Io.Vi.System_bus(function()
		mmc2 = wrap(hw.mmc2)
	end),
	nvmedrv = Io.Vi.System_bus({
		PCI0 = Io.Vi.PCI_bus({
			pci_hd = wrap(hw.pcie_fe150000),
		}),
	}),
})

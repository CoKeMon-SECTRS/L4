Io.add_vbus("ahci", Io.Vi.System_bus
	{
		ahci0 = wrap(Io.system_bus():match("PCI/CC_0106")), -- AHCI/SATA
	})

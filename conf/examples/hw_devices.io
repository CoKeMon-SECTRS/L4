-- vi:ft=lua

local Res = Io.Res
local Hw = Io.Hw
local Vi = Io.Vi
local root = Io.system_bus();
local sb = {}

local add_children = Io.Dt.add_children

add_children(Io.system_bus(), function()
	mmc0 = Hw.Device(function()
		compatible = "rockchip,rk3588-dw-mshc\0rockchip,rk3288-dw-mshc";
		Property.hid = "rockchip,rk3588-dw-mshc\0rockchip,rk3288-dw-mshc";
		Resource.reg0 = Res.mmio(0xfe2c0000, 0xFE2C3FFF);
		Resource.irq0 = Res.irq(32 + 0xcb, Io.Resource.Irq_type_level_high);
	end)

	mmc1 = Hw.Device(function()
		compatible = "rockchip,rk3588-dw-mshc\0rockchip,rk3288-dw-mshc";
		Property.hid = "rockchip,rk3588-dw-mshc\0rockchip,rk3288-dw-mshc";
		Resource.reg0 = Res.mmio(0xfe2d0000, 0xFE2D3FFF);
		Resource.irq0 = Res.irq(32 + 0xcc, Io.Resource.Irq_type_level_high);
	end)

	mmc2 = Hw.Device(function()
		compatible = "rockchip,rk3588-dwcmshc";
		Property.hid = "rockchip,rk3588-dwcmshc";
		Resource.reg0 = Res.mmio(0xfe2e0000, 0xfe2effff);
		Resource.irq0 = Res.irq(32 + 0xcd, Io.Resource.Irq_type_level_high);
	end)

	pcie_fe150000 = Io.Hw.Device(function()
		compatible    = { "rockchip,rk3588-pcie\0rockchip,rk3568-pcie" };
		Resource.reg0 = Res.mmio(0x0a40000000, 0x0a40000000 + 0x400000 - 1);
		Resource.reg1 = Res.mmio(0xfe150000, 0xfe150000 + 0x10000 - 1);
		Resource.reg2 = Res.mmio(0xf0000000, 0xf0000000 + 0x100000 - 1);
		Resource.irq0 = Res.irq(32 + 0x107, Io.Resource.Irq_type_level_high);
		Resource.irq1 = Res.irq(32 + 0x106, Io.Resource.Irq_type_level_high);
		Resource.irq2 = Res.irq(32 + 0x105, Io.Resource.Irq_type_level_high);
		Resource.irq3 = Res.irq(32 + 0x104, Io.Resource.Irq_type_level_high);
		Resource.irq4 = Res.irq(32 + 0x103, Io.Resource.Irq_type_level_high);
	end)
end)

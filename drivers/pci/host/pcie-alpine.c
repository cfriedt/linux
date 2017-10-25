/*
 * Annapurna Labs PCI host bridge device tree driver
 *
 * Copyright (c) 2017 Christopher Friedt <chrisfriedt@gmail.com>
 *
 * This file adapted from pcie-mediatek.c.
 *
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 *	   Honghui Zhang <honghui.zhang@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * - This driver for both internal PCIe bus and for external PCIe ports
 *   (in Root-Complex mode).
 * - The driver requires PCI_DOMAINS as each port registered as a pci domain
 * - for the external PCIe ports, the following applies:
 *	- Configuration access to bus 0 device 0 are routed to the configuration
 *	  space header register that found in the host bridge.
 *	- The driver assumes the controller link is initialized by the
 *	  bootloader.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

// defines

enum al_pci_type {
	AL_PCI_TYPE_INTERNAL = 0,
	AL_PCI_TYPE_EXTERNAL = 1,
};

struct al_pcie_port;

/**
 * struct al_pcie_port - PCIe port information
 * @base: IO mapped register base
 * @list: port list
 * @pcie: pointer to PCIe host info
 * @reset: pointer to port reset control
 * @sys_ck: pointer to transaction/data link layer clock
 * @ahb_ck: pointer to AHB slave interface operating clock for CSR access
 *          and RC initiated MMIO access
 * @axi_ck: pointer to application layer MMIO channel operating clock
 * @aux_ck: pointer to pe2_mac_bridge and pe2_mac_core operating clock
 *          when pcie_mac_ck/pcie_pipe_ck is turned off
 * @obff_ck: pointer to OBFF functional block operating clock
 * @pipe_ck: pointer to LTSSM and PHY/MAC layer operating clock
 * @phy: pointer to PHY control block
 * @lane: lane count
 * @slot: port slot
 * @irq_domain: legacy INTx IRQ domain
 * @msi_domain: MSI IRQ domain
 * @msi_irq_in_use: bit map for assigned MSI IRQ
 */
struct al_pcie_port {
	void __iomem *base;
	struct list_head list;
	struct al_pcie *pcie;
	struct reset_control *reset;
	struct clk *sys_ck;
	struct clk *ahb_ck;
	struct clk *axi_ck;
	struct clk *aux_ck;
	struct clk *obff_ck;
	struct clk *pipe_ck;
	struct phy *phy;
	u32 lane;
	u32 slot;
	struct irq_domain *irq_domain;
	struct irq_domain *msi_domain;
#ifndef MTK_MSI_IRQS_NUM
#define MTK_MSI_IRQS_NUM 1
#endif
	DECLARE_BITMAP(msi_irq_in_use, MTK_MSI_IRQS_NUM);
};

struct al_pcie {
	struct device *dev;
	void __iomem *base;
	struct clk *free_ck;

	struct resource io;
	struct resource pio;
	struct resource mem;
	struct resource busn;
	struct {
		resource_size_t mem;
		resource_size_t io;
	} offset;
	struct list_head ports;
	enum al_pci_type type;

	// old fields
	void __iomem *regs_base;
	struct resource regs;
	void __iomem *ecam_base;
	struct resource ecam;
	void __iomem *local_bridge_config_space;
};

static void al_pcie_subsys_powerdown(struct al_pcie *pcie)
{
	struct device *dev = pcie->dev;

	clk_disable_unprepare(pcie->free_ck);

	if (dev->pm_domain) {
		pm_runtime_put_sync(dev);
		pm_runtime_disable(dev);
	}
}

static void al_pcie_port_free(struct al_pcie_port *port)
{
	struct al_pcie *pcie = port->pcie;
	struct device *dev = pcie->dev;

	devm_iounmap(dev, port->base);
	list_del(&port->list);
	devm_kfree(dev, port);
}

static void al_pcie_put_resources(struct al_pcie *pcie)
{
	struct al_pcie_port *port, *tmp;

	list_for_each_entry_safe(port, tmp, &pcie->ports, list) {
		phy_power_off(port->phy);
		phy_exit(port->phy);
		clk_disable_unprepare(port->pipe_ck);
		clk_disable_unprepare(port->obff_ck);
		clk_disable_unprepare(port->axi_ck);
		clk_disable_unprepare(port->aux_ck);
		clk_disable_unprepare(port->ahb_ck);
		clk_disable_unprepare(port->sys_ck);
		al_pcie_port_free(port);
	}

	al_pcie_subsys_powerdown(pcie);
}

static int al_pcie_hw_rd_cfg(struct al_pcie_port *port, u32 bus, u32 devfn,
			      int where, int size, u32 *val)
{
	return PCIBIOS_SUCCESSFUL;
}

static int al_pcie_hw_wr_cfg(struct al_pcie_port *port, u32 bus, u32 devfn,
			      int where, int size, u32 val)
{
	return PCIBIOS_SUCCESSFUL;
}

static struct al_pcie_port *al_pcie_find_port(struct pci_bus *bus,
						unsigned int devfn)
{
	struct al_pcie *pcie = bus->sysdata;
	struct al_pcie_port *port;

	list_for_each_entry(port, &pcie->ports, list)
		if (port->slot == PCI_SLOT(devfn))
			return port;

	return NULL;
}

static int al_pcie_config_read(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 *val)
{
	struct al_pcie_port *port;
	u32 bn = bus->number;
	int ret;

	port = al_pcie_find_port(bus, devfn);
	if (!port) {
		*val = ~0;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	ret = al_pcie_hw_rd_cfg(port, bn, devfn, where, size, val);
	if (ret)
		*val = ~0;

	return ret;
}

static int al_pcie_config_write(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 val)
{
	struct al_pcie_port *port;
	u32 bn = bus->number;

	port = al_pcie_find_port(bus, devfn);
	if (!port)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return al_pcie_hw_wr_cfg(port, bn, devfn, where, size, val);
}

static struct pci_ops al_pcie_ops_v2 = {
	.read  = al_pcie_config_read,
	.write = al_pcie_config_write,
};

static int al_pcie_startup_port_v2(struct al_pcie_port *port)
{
	return 0;
}

static int al_pcie_msi_alloc(struct al_pcie_port *port)
{
	int msi;

	msi = find_first_zero_bit(port->msi_irq_in_use, MTK_MSI_IRQS_NUM);
	if (msi < MTK_MSI_IRQS_NUM)
		set_bit(msi, port->msi_irq_in_use);
	else
		return -ENOSPC;

	return msi;
}

static void al_pcie_msi_free(struct al_pcie_port *port, unsigned long hwirq)
{
	clear_bit(hwirq, port->msi_irq_in_use);
}

static int al_pcie_msi_setup_irq(struct msi_controller *chip,
				  struct pci_dev *pdev, struct msi_desc *desc)
{
	return 0;
}

static void al_msi_teardown_irq(struct msi_controller *chip, unsigned int irq)
{
	struct pci_dev *pdev = to_pci_dev(chip->dev);
	struct irq_data *d = irq_get_irq_data(irq);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	struct al_pcie_port *port;

	port = al_pcie_find_port(pdev->bus, pdev->devfn);
	if (!port)
		return;

	irq_dispose_mapping(irq);
	al_pcie_msi_free(port, hwirq);
}

static struct msi_controller al_pcie_msi_chip = {
	.setup_irq = al_pcie_msi_setup_irq,
	.teardown_irq = al_msi_teardown_irq,
};

static struct irq_chip al_msi_irq_chip = {
	.name = "MTK PCIe MSI",
	.irq_enable = pci_msi_unmask_irq,
	.irq_disable = pci_msi_mask_irq,
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

static int al_pcie_msi_map(struct irq_domain *domain, unsigned int irq,
			    irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &al_msi_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops msi_domain_ops = {
	.map = al_pcie_msi_map,
};

static void al_pcie_enable_msi(struct al_pcie_port *port)
{
}

static int al_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops intx_domain_ops = {
	.map = al_pcie_intx_map,
};

static int al_pcie_init_irq_domain(struct al_pcie_port *port,
				    struct device_node *node)
{
	struct device *dev = port->pcie->dev;
	struct device_node *pcie_intc_node;

	/* Setup INTx */
	pcie_intc_node = of_get_next_child(node, NULL);
	if (!pcie_intc_node) {
		dev_err(dev, "no PCIe Intc node found\n");
		return -ENODEV;
	}

	port->irq_domain = irq_domain_add_linear(pcie_intc_node, PCI_NUM_INTX,
						 &intx_domain_ops, port);
	if (!port->irq_domain) {
		dev_err(dev, "failed to get INTx IRQ domain\n");
		return -ENODEV;
	}

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		port->msi_domain = irq_domain_add_linear(node, MTK_MSI_IRQS_NUM,
							 &msi_domain_ops,
							 &al_pcie_msi_chip);
		if (!port->msi_domain) {
			dev_err(dev, "failed to create MSI IRQ domain\n");
			return -ENODEV;
		}
		al_pcie_enable_msi(port);
	}

	return 0;
}

static irqreturn_t al_pcie_intr_handler(int irq, void *data)
{
	struct al_pcie *pcie = (struct al_pcie *) data;
	struct device *dev = pcie->dev;

	return IRQ_HANDLED;
}

static int al_pcie_setup_irq(struct al_pcie_port *port,
			      struct device_node *node)
{
	struct al_pcie *pcie = port->pcie;
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	int err, irq;

	irq = platform_get_irq(pdev, port->slot);
	err = devm_request_irq(dev, irq, al_pcie_intr_handler,
			       IRQF_SHARED, "al-pcie", port);
	if (err) {
		dev_err(dev, "unable to request IRQ %d\n", irq);
		return err;
	}

	err = al_pcie_init_irq_domain(port, node);
	if (err) {
		dev_err(dev, "failed to init PCIe IRQ domain\n");
		return err;
	}

	return 0;
}

static int al_pcie_startup_port(struct al_pcie_port *port)
{
	return 0;
}

static void al_pcie_enable_port(struct al_pcie_port *port)
{
	struct al_pcie *pcie = port->pcie;
	struct device *dev = pcie->dev;
	int err;

	//dev_info( dev, "%s(): %d\n", __func__, __LINE__ );

/*
	err = clk_prepare_enable(port->sys_ck);
	if (err) {
		dev_err(dev, "failed to enable sys_ck%d clock\n", port->slot);
		goto err_sys_clk;
	}

	err = clk_prepare_enable(port->ahb_ck);
	if (err) {
		dev_err(dev, "failed to enable ahb_ck%d\n", port->slot);
		goto err_ahb_clk;
	}

	err = clk_prepare_enable(port->aux_ck);
	if (err) {
		dev_err(dev, "failed to enable aux_ck%d\n", port->slot);
		goto err_aux_clk;
	}

	err = clk_prepare_enable(port->axi_ck);
	if (err) {
		dev_err(dev, "failed to enable axi_ck%d\n", port->slot);
		goto err_axi_clk;
	}

	err = clk_prepare_enable(port->obff_ck);
	if (err) {
		dev_err(dev, "failed to enable obff_ck%d\n", port->slot);
		goto err_obff_clk;
	}

	err = clk_prepare_enable(port->pipe_ck);
	if (err) {
		dev_err(dev, "failed to enable pipe_ck%d\n", port->slot);
		goto err_pipe_clk;
	}

	reset_control_assert(port->reset);
	reset_control_deassert(port->reset);

	err = phy_init(port->phy);
	if (err) {
		dev_err(dev, "failed to initialize port%d phy\n", port->slot);
		goto err_phy_init;
	}

	err = phy_power_on(port->phy);
	if (err) {
		dev_err(dev, "failed to power on port%d phy\n", port->slot);
		goto err_phy_on;
	}

	if (!al_pcie_startup_port_v2(port))
		return;

	dev_info(dev, "Port%d link down\n", port->slot);

	phy_power_off(port->phy);
err_phy_on:
	phy_exit(port->phy);
err_phy_init:
	clk_disable_unprepare(port->pipe_ck);
err_pipe_clk:
	clk_disable_unprepare(port->obff_ck);
err_obff_clk:
	clk_disable_unprepare(port->axi_ck);
err_axi_clk:
	clk_disable_unprepare(port->aux_ck);
err_aux_clk:
	clk_disable_unprepare(port->ahb_ck);
err_ahb_clk:
	clk_disable_unprepare(port->sys_ck);
err_sys_clk:
	al_pcie_port_free(port);
	*/
}

static int al_pcie_parse_port(struct al_pcie *pcie,
			       struct device_node *node,
			       int slot)
{
	struct al_pcie_port *port;
	struct resource *regs;
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	char name[10];
	int err;

//	dev_info( dev, "%s(): %d\n", __func__, __LINE__ );

/*
	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	err = of_property_read_u32(node, "num-lanes", &port->lane);
	if (err) {
		dev_err(dev, "missing num-lanes property\n");
		return err;
	}

	snprintf(name, sizeof(name), "port%d", slot);
	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	port->base = devm_ioremap_resource(dev, regs);
	if (IS_ERR(port->base)) {
		dev_err(dev, "failed to map port%d base\n", slot);
		return PTR_ERR(port->base);
	}

	snprintf(name, sizeof(name), "sys_ck%d", slot);
	port->sys_ck = devm_clk_get(dev, name);
	if (IS_ERR(port->sys_ck)) {
		dev_err(dev, "failed to get sys_ck%d clock\n", slot);
		return PTR_ERR(port->sys_ck);
	}

	// sys_ck might be divided into the following parts in some chips
	snprintf(name, sizeof(name), "ahb_ck%d", slot);
	port->ahb_ck = devm_clk_get(dev, name);
	if (IS_ERR(port->ahb_ck)) {
		if (PTR_ERR(port->ahb_ck) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		port->ahb_ck = NULL;
	}

	snprintf(name, sizeof(name), "axi_ck%d", slot);
	port->axi_ck = devm_clk_get(dev, name);
	if (IS_ERR(port->axi_ck)) {
		if (PTR_ERR(port->axi_ck) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		port->axi_ck = NULL;
	}

	snprintf(name, sizeof(name), "aux_ck%d", slot);
	port->aux_ck = devm_clk_get(dev, name);
	if (IS_ERR(port->aux_ck)) {
		if (PTR_ERR(port->aux_ck) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		port->aux_ck = NULL;
	}

	snprintf(name, sizeof(name), "obff_ck%d", slot);
	port->obff_ck = devm_clk_get(dev, name);
	if (IS_ERR(port->obff_ck)) {
		if (PTR_ERR(port->obff_ck) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		port->obff_ck = NULL;
	}

	snprintf(name, sizeof(name), "pipe_ck%d", slot);
	port->pipe_ck = devm_clk_get(dev, name);
	if (IS_ERR(port->pipe_ck)) {
		if (PTR_ERR(port->pipe_ck) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		port->pipe_ck = NULL;
	}

	snprintf(name, sizeof(name), "pcie-rst%d", slot);
	port->reset = devm_reset_control_get_optional_exclusive(dev, name);
	if (PTR_ERR(port->reset) == -EPROBE_DEFER)
		return PTR_ERR(port->reset);

	// some platforms may use default PHY setting
	snprintf(name, sizeof(name), "pcie-phy%d", slot);
	port->phy = devm_phy_optional_get(dev, name);
	if (IS_ERR(port->phy))
		return PTR_ERR(port->phy);

	port->slot = slot;
	port->pcie = pcie;

	err = al_pcie_setup_irq(port, node);
	if (err)
		return err;

	INIT_LIST_HEAD(&port->list);
	list_add_tail(&port->list, &pcie->ports);
*/
	return 0;
}

static int al_pcie_subsys_powerup(struct al_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *regs;
	int err;

	dev_info( dev, "%s(): %d\n", __func__, __LINE__ );
/*
	// get shared registers, which are optional
	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "subsys");
	if (regs) {
		pcie->base = devm_ioremap_resource(dev, regs);
		if (IS_ERR(pcie->base)) {
			dev_err(dev, "failed to map shared register\n");
			return PTR_ERR(pcie->base);
		}
	}

	pcie->free_ck = devm_clk_get(dev, "free_ck");
	if (IS_ERR(pcie->free_ck)) {
		if (PTR_ERR(pcie->free_ck) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		pcie->free_ck = NULL;
	}

	if (dev->pm_domain) {
		pm_runtime_enable(dev);
		pm_runtime_get_sync(dev);
	}

	// enable top level clock
	err = clk_prepare_enable(pcie->free_ck);
	if (err) {
		dev_err(dev, "failed to enable free_ck\n");
		goto err_free_ck;
	}

	return 0;

err_free_ck:
	if (dev->pm_domain) {
		pm_runtime_put_sync(dev);
		pm_runtime_disable(dev);
	}
*/
	err = 0;
	return err;
}

static int al_pcie_setup(struct al_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct device_node *node = dev->of_node, *child;
	struct of_pci_range_parser parser;
	struct of_pci_range range;
	struct resource res;
	struct al_pcie_port *port, *tmp;
	int err;

	if (of_pci_range_parser_init(&parser, node)) {
		dev_err(dev, "missing \"ranges\" property\n");
		return -EINVAL;
	}

	if (pcie->type == AL_PCI_TYPE_EXTERNAL) {
		/* Get registers resources */
		err = of_address_to_resource(node, 0, &pcie->regs);
		if (err < 0) {
			dev_err(pcie->dev, "of_address_to_resource(): %d\n",
				err);
			return err;
		}
		dev_info(pcie->dev, " regs %pR\n",  &pcie->regs);
		pcie->regs_base = devm_ioremap_resource(pcie->dev,
							   &pcie->regs);
		if (IS_ERR(pcie->regs_base))
			return PTR_ERR(pcie->regs_base);
		/* set the base address of the configuration space of the local
		 * bridge
		 */
		pcie->local_bridge_config_space = pcie->regs_base + 0x2000;
	}

	for_each_of_pci_range(&parser, &range) {
		err = of_pci_range_to_resource(&range, node, &res);
		if (err < 0)
			return err;

		switch (res.flags & IORESOURCE_TYPE_BITS) {
		case 0:

			memcpy(&pcie->ecam, &res, sizeof(res));

			pcie->ecam.start = range.cpu_addr;
			pcie->ecam.end = range.cpu_addr + range.size - 1;
			pcie->ecam.flags = IORESOURCE_MEM;
			pcie->ecam.name = "ECAM";
			break;

		case IORESOURCE_IO:
			pcie->offset.io = res.start - range.pci_addr;

			memcpy(&pcie->pio, &res, sizeof(res));
			pcie->pio.name = node->full_name;

			pcie->io.start = range.cpu_addr;
			pcie->io.end = range.cpu_addr + range.size - 1;
			pcie->io.flags = IORESOURCE_MEM;
			pcie->io.name = "I/O";

			memcpy(&res, &pcie->io, sizeof(res));
			break;

		case IORESOURCE_MEM:
			pcie->offset.mem = res.start - range.pci_addr;

			memcpy(&pcie->mem, &res, sizeof(res));
			pcie->mem.name = "non-prefetchable";
			break;
		}
	}

	err = of_pci_parse_bus_range(node, &pcie->busn);
	if (err < 0) {
		dev_err(dev, "failed to parse bus ranges property: %d\n", err);
		pcie->busn.name = node->name;
		pcie->busn.start = 0;
		pcie->busn.end = 0xff;
		pcie->busn.flags = IORESOURCE_BUS;
	}

	for_each_available_child_of_node(node, child) {
		int slot;

		err = of_pci_get_devfn(child);
		if (err < 0) {
			dev_err(dev, "failed to parse devfn: %d\n", err);
			return err;
		}

		slot = PCI_SLOT(err);

		err = al_pcie_parse_port(pcie, child, slot);
		if (err) {
			dev_err( dev, "%s(): %d: al_pcie_parse_port() failed %d\n", __func__, __LINE__, err );
			return err;
		}
	}

	err = al_pcie_subsys_powerup(pcie);
	if (err) {
		dev_err( dev, "%s(): %d: al_pcie_subsys_powerup() failed %d\n", __func__, __LINE__, err );
		return err;
	}

	/* enable each port, and then check link status */
	list_for_each_entry_safe(port, tmp, &pcie->ports, list)
		al_pcie_enable_port(port);

	/* power down PCIe subsys if slots are all empty (link down) */
	if (list_empty(&pcie->ports))
		al_pcie_subsys_powerdown(pcie);

	dev_err( dev, "%s(): %d: return %d\n", __func__, __LINE__, 0 );

	return 0;
}

static int al_pcie_request_resources(struct al_pcie *pcie)
{
	struct pci_host_bridge *host = pci_host_bridge_from_priv(pcie);
	struct list_head *windows = &host->windows;
	struct device *dev = pcie->dev;
	int err;

	pci_add_resource_offset(windows, &pcie->pio, pcie->offset.io);
	pci_add_resource_offset(windows, &pcie->mem, pcie->offset.mem);
	pci_add_resource(windows, &pcie->busn);

	err = devm_request_pci_bus_resources(dev, windows);
	if (err < 0) {
		dev_err( dev, "%s(): %d: pci_scan_root_bus_bridge() failed %d\n", __func__, __LINE__, err );
		return err;
	}

	pci_remap_iospace(&pcie->pio, pcie->io.start);

	dev_err( dev, "%s(): %d: return %d\n", __func__, __LINE__, 0 );

	return 0;
}

static int al_pcie_register_host(struct pci_host_bridge *host)
{
	struct al_pcie *pcie = pci_host_bridge_priv(host);
	struct pci_bus *child;
	int err;

	host->busnr = pcie->busn.start;
	host->dev.parent = pcie->dev;
	host->ops = & al_pcie_ops_v2;
	host->map_irq = of_irq_parse_and_map_pci;
	host->swizzle_irq = pci_common_swizzle;
	host->sysdata = pcie;
//	if (IS_ENABLED(CONFIG_PCI_MSI) && pcie->type->has_msi)
//		host->msi = &al_pcie_msi_chip;

	struct device *dev = pcie->dev;

	err = pci_scan_root_bus_bridge(host);
	if (err < 0) {
		dev_err( dev, "%s(): %d: pci_scan_root_bus_bridge() failed %d\n", __func__, __LINE__, err );
		return err;
	}

	pci_bus_size_bridges(host->bus);
	pci_bus_assign_resources(host->bus);

	list_for_each_entry(child, &host->bus->children, node)
		pcie_bus_configure_settings(child);

	pci_bus_add_devices(host->bus);

	dev_err( dev, "%s(): %d: return %d\n", __func__, __LINE__, 0 );

	return 0;
}

static int al_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct al_pcie *pcie;
	struct pci_host_bridge *host;
	int err;

	host = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!host)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(host);

	pcie->dev = dev;
	pcie->type = (enum al_pci_type) of_device_get_match_data(dev);
	platform_set_drvdata(pdev, pcie);
	INIT_LIST_HEAD(&pcie->ports);

	err = al_pcie_setup(pcie);
	if (err) {
		dev_err( dev, "%s(): %d: al_pcie_setup() failed\n", __func__, __LINE__ );
		return err;
	}

	err = al_pcie_request_resources(pcie);
	if (err) {
		dev_err( dev, "%s(): %d: al_pcie_request_resources() failed\n", __func__, __LINE__ );
		goto put_resources;
	}

	err = al_pcie_register_host(host);
	if (err) {
		dev_err( dev, "%s(): %d: al_pcie_register_host() failed\n", __func__, __LINE__ );
		goto put_resources;
	}

	dev_info( dev, "%s(): %d: returning %d\n", __func__, __LINE__, 0 );

	return 0;

put_resources:
	if (!list_empty(&pcie->ports))
		al_pcie_put_resources(pcie);

	dev_err( dev, "%s(): %d: returning %d\n", __func__, __LINE__, err );

	return err;
}

static const struct of_device_id al_pcie_ids[] = {
	{ .compatible = "annapurna-labs,al-pci", .data = (void *)AL_PCI_TYPE_EXTERNAL },
	{ .compatible = "annapurna-labs,al-internal-pcie", .data = (void *)AL_PCI_TYPE_INTERNAL },
	{ },
};

static struct platform_driver al_pcie_driver = {
	.probe = al_pcie_probe,
	.driver = {
		.name = "al-pcie",
		.of_match_table = al_pcie_ids,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(al_pcie_driver);

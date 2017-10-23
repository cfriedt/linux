#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define DEBUG

#ifndef range_iter_fill_resource
#define range_iter_fill_resource(iter, np, res) \
        do { \
                (res)->flags = (iter).flags; \
                (res)->start = (iter).cpu_addr; \
                (res)->end = (iter).cpu_addr + (iter).size - 1; \
                (res)->parent = (res)->child = (res)->sibling = NULL; \
                (res)->name = (np)->full_name; \
        } while (0)
#endif

enum al_pci_type {
	AL_PCI_TYPE_INTERNAL,
	AL_PCI_TYPE_EXTERNAL,
};

struct al_pcie_pd {
	struct platform_device *pdev;

//	struct resource ecam;
//	struct resource mem;
//	struct resource io;
//	struct resource realio;
//	struct resource regs;
//	struct resource busn;
//
	enum al_pci_type type;

	void __iomem	 *ecam_base;
	void __iomem	 *regs_base;
	// base address of the configuration space of the local bridge
	void __iomem	 *bcfg_base;

	int irq;
	u8 root_bus_nr;
	struct irq_domain *irq_domain;
	struct resource bus_range;
	struct list_head	resources;
};

static bool al_pcie_link_is_up(struct al_pcie_pd *pcie)
{
	return false;
	//return !!((cra_readl(pcie, RP_LTSSM) & RP_LTSSM_MASK) == LTSSM_L0);
}

static bool al_pcie_valid_device(struct al_pcie_pd *pcie,
				     struct pci_bus *bus, int dev)
{
	/* If there is no link, then there is no device */
	if (bus->number != pcie->root_bus_nr) {
		if (!al_pcie_link_is_up(pcie))
			return false;
	}

	/* access only one slot on each root port */
	if (bus->number == pcie->root_bus_nr && dev > 0)
		return false;

	 return true;
}

static int al_pcie_cfg_read(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 *value)
{
	struct al_pcie_pd *pcie = bus->sysdata;

//	if (al_pcie_hide_rc_bar(bus, devfn, where))
//		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (!al_pcie_valid_device(pcie, bus, PCI_SLOT(devfn))) {
		*value = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

//	return _al_pcie_cfg_read(pcie, bus->number, devfn, where, size,
//				     value);
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static int al_pcie_cfg_write(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 value)
{
	struct al_pcie_pd *pcie = bus->sysdata;

//	if (al_pcie_hide_rc_bar(bus, devfn, where))
//		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (!al_pcie_valid_device(pcie, bus, PCI_SLOT(devfn)))
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_DEVICE_NOT_FOUND;
//	return _al_pcie_cfg_write(pcie, bus->number, devfn, where, size,
//				     value);
}

static struct pci_ops al_pcie_ops = {
	.read = al_pcie_cfg_read,
	.write = al_pcie_cfg_write,
};

static void al_pcie_retrain(struct al_pcie_pd *pcie)
{
	u16 linkcap, linkstat, linkctl;

	if (!al_pcie_link_is_up(pcie))
		return;

	/*
	 * Set the retrain bit if the PCIe rootport support > 2.5GB/s, but
	 * current speed is 2.5 GB/s.
	 */
//	al_read_cap_word(pcie, pcie->root_bus_nr, RP_DEVFN, PCI_EXP_LNKCAP,
//			     &linkcap);
//	if ((linkcap & PCI_EXP_LNKCAP_SLS) <= PCI_EXP_LNKCAP_SLS_2_5GB)
//		return;

//	al_read_cap_word(pcie, pcie->root_bus_nr, RP_DEVFN, PCI_EXP_LNKSTA,
//			     &linkstat);
//	if ((linkstat & PCI_EXP_LNKSTA_CLS) == PCI_EXP_LNKSTA_CLS_2_5GB) {
//		al_read_cap_word(pcie, pcie->root_bus_nr, RP_DEVFN,
//				     PCI_EXP_LNKCTL, &linkctl);
//		linkctl |= PCI_EXP_LNKCTL_RL;
//		al_write_cap_word(pcie, pcie->root_bus_nr, RP_DEVFN,
//				      PCI_EXP_LNKCTL, linkctl);
//
//		al_wait_link_retrain(pcie);
//	}
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
	.xlate = pci_irqd_intx_xlate,
};

static irqreturn_t al_pcie_isr( int irq, void *data ) {

	struct al_pcie_pd *pcie = (struct al_pcie_pd *) data;
	struct platform_device *pdev = pcie->pdev;
	struct device *dev = & pdev->dev;

	dev_dbg( dev, "interrupt received\n" );

	return IRQ_HANDLED;
}

static int al_pice_get_ecam_resource( struct platform_device *pdev, struct resource *res ) {

	struct of_pci_range_parser parser;
	struct of_pci_range iter;

	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	int r, i;

	dev_info( dev, "In %s()\n", __func__ );

	r = of_pci_range_parser_init( & parser, np );
	if ( r ) {
		dev_err( dev, "of_pci_range_parser_init() %d\n", r );
		return r;
	}

	i = 0;
	r = -ENXIO;
	for_each_of_pci_range( & parser, & iter ) {
		dev_info( dev,
			"pci_range[ %d ]: pci_space: %x pci_addr: %llx cpu_addr: %llx size: %llx flags: %x\n",
			i,
			iter.pci_space,
			iter.pci_addr,
			iter.cpu_addr,
			iter.size,
			iter.flags
		);
		if ( 0 == ( iter.flags & IORESOURCE_TYPE_BITS ) ) {
			dev_info( dev, "found ecam resource\n" );
			range_iter_fill_resource( iter, np, res );
			res->flags = IORESOURCE_MEM;
			res->name = "ECAM";
			r = 0;
		}
		i++;
	}

	return r;
}

static int al_pcie_parse_dt( struct al_pcie_pd *pcie )
{
	enum al_pci_type type;
	struct resource res;

	struct platform_device *pdev = pcie->pdev;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	int r;

	dev_info( dev, "In %s()\n", __func__ );

	/* map resources */

	type = (enum al_pci_type) np->data;

	if ( AL_PCI_TYPE_EXTERNAL == type ) {

		r = of_address_to_resource( np, 0, & res );
		if ( r < 0 ) {
			dev_err( dev, "of_address_to_resource(): %d\n", r );
			return r;
		}

		pcie->regs_base = devm_ioremap_resource( dev, & res );
		if ( IS_ERR( pcie->regs_base ) ) {
			return PTR_ERR( pcie->regs_base );
		}

		pcie->bcfg_base = pcie->regs_base + 0x2000;

		dev_info( dev, "regs_base: %p, bcfg_base: %p\n", pcie->regs_base, pcie->bcfg_base );
	}

	r = al_pice_get_ecam_resource( pdev, & res );
	if ( r ) {
		dev_err( dev, "failed to get ecam resource\n" );
		return r;
	}

	pcie->ecam_base = devm_ioremap_resource( dev, & res );
	if ( IS_ERR( pcie->ecam_base ) ) {
		return PTR_ERR( pcie->ecam_base );
	}

	dev_info( dev, "ecam_base: %p\n", pcie->ecam_base );

	/* setup irq */
/*
	pcie->irq = platform_get_irq(pdev, 0);
	if (pcie->irq < 0) {
		dev_err(dev, "failed to get IRQ: %d\n", pcie->irq);
		return pcie->irq;
	}

	r = devm_request_irq( dev, pcie->irq, al_pcie_isr, IRQF_SHARED, "pcie-sys", pcie );
	if ( r ) {
		dev_err(dev, "failed to request PCIe subsystem IRQ\n");
		return r;
	}
*/

	return 0;
}

static int al_pcie_parse_request_of_pci_ranges(struct al_pcie_pd *pcie)
{
	int err, res_valid = 0;
	struct device *dev = &pcie->pdev->dev;
	struct device_node *np = dev->of_node;
	struct resource_entry *win;

	err = of_pci_get_host_bridge_resources(np, 0, 0xff, &pcie->resources,
					       NULL);
	if (err)
		return err;

	err = devm_request_pci_bus_resources(dev, &pcie->resources);
	if (err)
		goto out_release_res;

	resource_list_for_each_entry(win, &pcie->resources) {
		struct resource *res = win->res;

		if (resource_type(res) == IORESOURCE_MEM)
			res_valid |= !(res->flags & IORESOURCE_PREFETCH);
	}

	if (res_valid)
		return 0;

	dev_err(dev, "non-prefetchable memory resource required\n");
	err = -EINVAL;

out_release_res:
	pci_free_resource_list(&pcie->resources);
	return err;
}

static int al_pcie_init_irq_domain(struct al_pcie_pd *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	struct device_node *node = dev->of_node;

	/* Setup INTx */
	pcie->irq_domain = irq_domain_add_linear(node, PCI_NUM_INTX,
					&intx_domain_ops, pcie);
	if (!pcie->irq_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return -ENOMEM;
	}

	return 0;
}

static void al_pcie_host_init(struct al_pcie_pd *pcie)
{
	al_pcie_retrain(pcie);
}

static int al_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct al_pcie_pd *pcie;
	struct pci_bus *bus;
	struct pci_bus *child;
	struct pci_host_bridge *bridge;
	int ret;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!bridge)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(bridge);
	pcie->pdev = pdev;

	ret = al_pcie_parse_dt(pcie);
	if (ret) {
		dev_err(dev, "Parsing DT failed\n");
		return ret;
	}

	INIT_LIST_HEAD(&pcie->resources);

	ret = al_pcie_parse_request_of_pci_ranges(pcie);
	if (ret) {
		dev_err(dev, "Failed add resources\n");
		return ret;
	}

	ret = al_pcie_init_irq_domain(pcie);
	if (ret) {
		dev_err(dev, "Failed creating IRQ Domain\n");
		return ret;
	}

	/* clear all interrupts */
	//cra_writel(pcie, P2A_INT_STS_ALL, P2A_INT_STATUS);
	/* enable all interrupts */
	//cra_writel(pcie, P2A_INT_ENA_ALL, P2A_INT_ENABLE);
	al_pcie_host_init(pcie);

	list_splice_init(&pcie->resources, &bridge->windows);
	bridge->dev.parent = dev;
	bridge->sysdata = pcie;
	bridge->busnr = pcie->root_bus_nr;
	bridge->ops = &al_pcie_ops;
	bridge->map_irq = of_irq_parse_and_map_pci;
	bridge->swizzle_irq = pci_common_swizzle;

	ret = pci_scan_root_bus_bridge(bridge);
	if (ret < 0)
		return ret;

	bus = bridge->bus;

	pci_assign_unassigned_bus_resources(bus);

	/* Configure PCI Express setting. */
	list_for_each_entry(child, &bus->children, node)
		pcie_bus_configure_settings(child);

	pci_bus_add_devices(bus);
	return ret;
}

static const struct of_device_id al_pcie_of_match[] = {
	{ .compatible = "annapurna-labs,al-pci", .data = (void *)AL_PCI_TYPE_EXTERNAL },
	{ .compatible = "annapurna-labs,al-internal-pcie", .data = (void *)AL_PCI_TYPE_INTERNAL },
	{ },
};

static struct platform_driver al_pcie_driver = {
	.driver = {
		.name = "al-pcie",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(al_pcie_of_match),
	},
	.probe = al_pcie_probe,
};
module_platform_driver(al_pcie_driver);

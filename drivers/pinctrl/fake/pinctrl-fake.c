/*
 * Fake-pinctrl driver
 *
 * Copyright (C) 2016, Christopher Friedt
 * Author: Christopher Friedt <chrisfriedt@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "linux/pinctrl-fake.h"

#ifndef MODULE_DESC
#define MODULE_DESC "Fake Pinctrl Driver"
#endif

#ifndef _pr_info
#define _pr_info( fmt, args... ) pr_info( MODULE_DESC ": " fmt, ##args )
#endif

#ifndef _pr_err
#define _pr_err( fmt, args... ) pr_err( MODULE_DESC ": " fmt, ##args )
#endif

#ifndef _pr_dbg
#define _pr_dbg( fmt, args... ) pr_devel( MODULE_DESC ": " fmt, ##args )
#endif

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif // EXIT_SUCCESS

static int pinctrl_fake_get_groups_count(struct pinctrl_dev *pctldev);
static const char *pinctrl_fake_get_group_name(struct pinctrl_dev *pctldev,
				      unsigned group);
static int pinctrl_fake_get_group_pins(struct pinctrl_dev *pctldev, unsigned group,
			      const unsigned **pins, unsigned *npins);
static void pinctrl_fake_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
			     unsigned offset);

static int pinctrl_fake_get_functions_count(struct pinctrl_dev *pctldev);
static const char *pinctrl_fake_get_function_name(struct pinctrl_dev *pctldev,
					 unsigned function);
static int pinctrl_fake_get_function_groups(struct pinctrl_dev *pctldev,
				   unsigned function,
				   const char *const **groups,
				   unsigned *const ngroups);
static int pinctrl_fake_pinmux_set_mux(struct pinctrl_dev *pctldev, unsigned function,
			      unsigned group);

static int pinctrl_fake_config_get(struct pinctrl_dev *pctldev, unsigned pin,
			  unsigned long *config);
static int pinctrl_fake_config_set(struct pinctrl_dev *pctldev, unsigned pin,
			  unsigned long *configs, unsigned nconfigs);

static const struct pinctrl_ops pinctrl_fake_ops = {
	.get_groups_count = pinctrl_fake_get_groups_count,
	.get_group_name = pinctrl_fake_get_group_name,
	.get_group_pins = pinctrl_fake_get_group_pins,
	.pin_dbg_show = pinctrl_fake_pin_dbg_show,
};

static const struct pinmux_ops pinctrl_fake_pinmux_ops = {
	.get_functions_count = pinctrl_fake_get_functions_count,
	.get_function_name = pinctrl_fake_get_function_name,
	.get_function_groups = pinctrl_fake_get_function_groups,
	.set_mux = pinctrl_fake_pinmux_set_mux,
};

static const struct pinconf_ops pinctrl_fake_pinconf_ops = {
	.is_generic = true,
	.pin_config_set = pinctrl_fake_config_set,
	.pin_config_get = pinctrl_fake_config_get,
};

static const struct pinctrl_fake pinctrl_fake_template = {
	.pctldesc = {
		.pctlops = &pinctrl_fake_ops,
		.pmxops = &pinctrl_fake_pinmux_ops,
		.confops = &pinctrl_fake_pinconf_ops,
		.owner = THIS_MODULE,
	},
};

static void pinctrl_fake_free( struct device *dev, struct pinctrl_fake **ppctrl );

static struct pinctrl_fake *pinctrl_fake_allocate_from_dt( struct device *dev, struct device_node *np ) {
	struct pinctrl_fake *pctrl;

	int i, j, r;
	u32 pin_number;

	char buf[ 64 ];
	char **charp;

	dev_info( dev, "%s()\n", __FUNCTION__ );

	pctrl = kzalloc( sizeof( struct pinctrl_fake ), GFP_KERNEL );
	if ( NULL == pctrl ) {
		dev_err( dev, "unable to allocate 'struct pinctrl_fake'\n" );
		r = -ENOMEM;
		goto out;
	}
	memcpy( pctrl, & pinctrl_fake_template, sizeof( pinctrl_fake_template ) );
	pctrl->pctldesc.name = dev_name( dev );

	//
	// pins
	//

	r = of_property_count_u32_elems(  np, "pinctrl-fake-pins" );
	if ( r < 0 ) {
		dev_err( dev, "unable to determine length of 'pinctrl-fake-pins' array\n" );
		goto free_pctrl;
	}
	if ( 0 == r ) {
		dev_err( dev, "'pinctrl-fake-pins' array is zero-length\n" );
		r = -EINVAL;
		goto free_pctrl;
	}
	pctrl->pctldesc.npins = r;

	dev_dbg( dev, "allocating %u pins\n", pctrl->pctldesc.npins );
	pctrl->pctldesc.pins = kzalloc( pctrl->pctldesc.npins * sizeof( struct pinctrl_pin_desc ), GFP_KERNEL );
	if ( NULL == pctrl->pctldesc.pins ) {
		dev_err( dev, "unable to allocate array of 'struct pinctrl_pin_desc'\n" );
		r = -ENOMEM;
		goto free_pctrl;
	}

	for( i = 0; i < pctrl->pctldesc.npins; i++ ) {

		r = of_property_read_u32_index( np, "pinctrl-fake-pins", i, & pin_number );
		if ( EXIT_SUCCESS != r ) {
			dev_err( dev, "unable to read 'pin-numbers' at index %d\n", i );
			goto free_pctrl;
		}
		*((unsigned *) & pctrl->pctldesc.pins[ i ].number) = (unsigned) pin_number;

		r = of_property_read_string_index( np, "pinctrl-fake-pin-names", i, (const char **)& pctrl->pctldesc.pins[ i ].name );
		if ( EXIT_SUCCESS != r ) {
			dev_err( dev, "unable to read 'pinctrl-fake-names' at index %d\n", i );
			goto free_pctrl;
		}

		dev_dbg( dev, "pin[ %u ]: %u '%s'\n", i, pctrl->pctldesc.pins[ i ].number, pctrl->pctldesc.pins[ i ].name );

		pinctrl_pin_desc_attach_pinctrl_fake( (struct pinctrl_pin_desc *) & pctrl->pctldesc.pins[ i ], pctrl );
	}

	//
	// groups
	//

	r = of_property_count_strings(  np, "pinctrl-fake-pin-groups" );
	if ( r <= 0 ) {
		dev_warn( dev, "'pinctrl-fake-pin-groups' array not found or is zero-length\n" );
		r = 0;
	}
	pctrl->ngroups = r;


	if ( 0 == pctrl->ngroups ) {
		r = -EINVAL;
		goto free_pctrl;
	}

	dev_dbg( dev, "allocating %u groups\n", pctrl->ngroups );
	pctrl->groups = kzalloc( pctrl->ngroups * sizeof( struct pinctrl_fake_group ), GFP_KERNEL );
	if ( NULL == pctrl->groups ) {
		dev_err( dev, "unable to allocate array of 'struct pinctrl_fake_group'\n" );
		r = -ENOMEM;
		goto free_pctrl;
	}

	for( i = 0; i < pctrl->ngroups; i++ ) {

		r = of_property_read_string_index( np, "pinctrl-fake-pin-groups", i, (const char **) & pctrl->groups[ i ].name );
		if ( r < 0 || 0 == strlen( pctrl->groups[ i ].name ) ) {
			dev_err( dev, "unable to read 'pinctrl-fake-pin-groups' array at index %d\n", i );
			goto free_pctrl;
		}

		memset( buf, 0, sizeof( buf ) );
		snprintf( buf, sizeof( buf ) - 1, "pinctrl-fake-pin-group-%u", i );

		r = of_property_count_u32_elems( np, buf );
		if ( r <= 0 ) {
			dev_err( dev, "unable to determine length of '%s' array for group '%s'\n", buf, pctrl->groups[ i ].name );
			r = 0 == r ? -EINVAL : r;
			goto free_pctrl;
		}
		pctrl->groups[ i ].npins = r;

		dev_dbg( dev, "allocating %u pins for group %u '%s'\n", r, i, pctrl->groups[ i ].name );

		pctrl->groups[ i ].pins = kzalloc( pctrl->groups[ i ].npins * sizeof( unsigned ), GFP_KERNEL );
		if ( NULL == pctrl->groups[ i ].pins ) {
			dev_err( dev, "unable to allocate memory for group '%s' pins\n", pctrl->groups[ i ].name );
			r = -ENOMEM;
			goto free_pctrl;
		}

		for( j = 0; j < pctrl->groups[ i ].npins; j++ ) {
			r = of_property_read_u32_index( np, (const char *) buf, j, & pin_number );
			if ( r < 0 ) {
				dev_err( dev, "unable to read '%s' array at index %d\n", buf, j );
				goto free_pctrl;
			}
			pctrl->groups[ i ].pins[ j ] = pin_number;

			dev_dbg( dev, "group[ %u ]: '%s', pin[ %u ]: %u\n", i, pctrl->groups[ i ].name, j, pctrl->groups[ i ].pins[ j ] );
		}
	}

	//
	// muxes / functions
	//

	r = of_property_count_strings(  np, "pinctrl-fake-pin-muxes" );
	if ( r <= 0 ) {
		dev_warn( dev, "'pinctrl-fake-pin-muxes' array not found or is zero-length\n" );
		r = 0;
	}
	pctrl->nmuxes = r;

	if ( 0 == pctrl->nmuxes ) {
		dev_err( dev, "'pinctrl-fake-pin-muxes' is zero-length\n" );
		r = -EINVAL;
		goto free_pctrl;
	}

	dev_dbg( dev, "allocating %u muxes\n", pctrl->nmuxes );
	pctrl->muxes = kzalloc( pctrl->nmuxes * sizeof( struct pinctrl_fake_pmx_func ), GFP_KERNEL );
	if ( NULL == pctrl->muxes ) {
		dev_err( dev, "unable to allocate array of 'struct pinctrl_fake_pmx_func'\n" );
		r = -ENOMEM;
		goto free_pctrl;
	}

	for( i = 0; i < pctrl->nmuxes; i++ ) {

		r = of_property_read_string_index( np, "pinctrl-fake-pin-muxes", i, (const char **) & pctrl->muxes[ i ].name );
		if ( r < 0 || 0 == strlen( pctrl->muxes[ i ].name ) ) {
			dev_err( dev, "unable to read 'pinctrl-fake-pin-muxes' array at index %d\n", i );
			goto free_pctrl;
		}

		memset( buf, 0, sizeof( buf ) );
		snprintf( buf, sizeof( buf ) - 1, "pinctrl-fake-pin-mux-%u", i );

		r = of_property_count_strings( np, buf );
		if ( r <= 0 ) {
			dev_err( dev, "unable to determine length of '%s' array for group '%s'\n", buf, pctrl->muxes[ i ].name );
			r = 0 == r ? -EINVAL : r;
			goto free_pctrl;
		}
		pctrl->muxes[ i ].ngroups = r;

		pin_number = pctrl->muxes[ i ].ngroups * sizeof( char * );

		dev_dbg( dev, "allocating %u groups for mux %u '%s' (%u bytes)\n", pctrl->muxes[ i ].ngroups, i, pctrl->muxes[ i ].name, pin_number );

		pctrl->muxes[ i ].groups = kzalloc( pin_number, GFP_KERNEL );
		if ( NULL == pctrl->muxes[ i ].groups ) {
			dev_err( dev, "unable to allocate memory for mux '%s' groups\n", pctrl->muxes[ i ].name );
			r = -ENOMEM;
			goto free_pctrl;
		}

		for( j = 0; j < pctrl->muxes[ i ].ngroups; j++ ) {
			dev_dbg( dev, "of_property_read_string_index() of prop '%s' at index %u\n", buf, j );
			r = of_property_read_string_index( np, (const char *) buf, j, (const char **) & pctrl->muxes[ i ].groups[ j ] );
			if ( r < 0 ) {
				dev_err( dev, "unable to read '%s' array at index %d\n", buf, j );
				goto free_pctrl;
			}
			dev_dbg( dev, "mux[ %u ]: '%s', group[ %u ]: '%s'\n", i, pctrl->muxes[ i ].name, j, pctrl->muxes[ i ].groups[ j ] );
		}
	}

	//
	// mappings
	//

	r = of_property_count_strings(  np, "pinctrl-fake-mappings" );
	if ( r <= 0 ) {
		dev_warn( dev, "'pinctrl-fake-mappings' array not found or is zero-length\n" );
		r = 0;
	}
	pctrl->nmappings = r;

	if ( 0 != pctrl->nmappings % 4 ) {
		pctrl->nmappings = 0;
		dev_err( dev, "'pinctrl-fake-mappings' array length is not a multiple of 4\n" );
		r = -EINVAL;
		goto free_pctrl;
	}
	pctrl->nmappings /= 4;

	if ( 0 == pctrl->nmappings ) {
		dev_err( dev, "'pinctrl-fake-mappings' is zero-length\n" );
		r = -EINVAL;
		goto free_pctrl;
	}

	dev_dbg( dev, "allocating %u mappings\n", pctrl->nmappings );
	pctrl->mappings = kzalloc( pctrl->nmappings * sizeof( struct pinctrl_map ), GFP_KERNEL );
	if ( NULL == pctrl->mappings ) {
		dev_err( dev, "unable to allocate array of 'struct pinctrl_map'\n" );
		r = -ENOMEM;
		goto free_pctrl;
	}

	for( i = 0; i < pctrl->nmappings; i++ ) {

		pctrl->mappings[ i ].ctrl_dev_name = dev_name( dev );
		pctrl->mappings[ i ].type = PIN_MAP_TYPE_MUX_GROUP;

		for( j = 0; j < 4; j++ ) {
			switch( j ) {
			case 0:
				charp = (char **) & pctrl->mappings[ i ].dev_name;
				break;
			case 1:
				charp = (char **) & pctrl->mappings[ i ].name;
				break;
			case 2:
				charp = (char **) & pctrl->mappings[ i ].data.mux.function;
				break;
			case 3:
				charp = (char **) & pctrl->mappings[ i ].data.mux.group;
				break;
			default:
				BUG_ON( j != 0 );
				break;
			}

			r = of_property_read_string_index( np, "pinctrl-fake-mappings", i*4 + j, (const char **) charp );
			if ( r < 0 || 0 == strlen( *charp ) ) {
				dev_err( dev, "unable to read 'pinctrl-fake-mappings' array at index %d\n", i*4 + j );
				goto free_pctrl;
			}
			dev_dbg( dev, "'pinctrl-fake-mappings'[ %u ] = '%s'\n", i*4 + j, *charp );
		}
	}

	r = EXIT_SUCCESS;
	goto out;

free_pctrl:
	pinctrl_fake_free( dev, & pctrl );

out:
	if ( EXIT_SUCCESS != r ) {
		pctrl = ERR_PTR( r );
	}

	return pctrl;
}

static void pinctrl_fake_free( struct device *dev, struct pinctrl_fake **ppctrl ) {

	struct pinctrl_fake *pctrl;
	int i;

	dev_info( dev, "%s()\n", __FUNCTION__ );

	if ( NULL == ppctrl || NULL == ( pctrl = *ppctrl ) ) {
		goto out;
	}

	if ( pctrl->nmappings > 0 && NULL != pctrl->mappings ) {
		dev_dbg( dev, "freeing %u mappings\n", pctrl->nmappings );
		kfree( pctrl->mappings );
		pctrl->mappings = NULL;
		pctrl->nmappings = 0;
	}

	if ( pctrl->nmuxes > 0 && NULL != pctrl->muxes ) {
		dev_dbg( dev, "freeing %u muxes\n", pctrl->nmuxes );
		for( i = 0; i < pctrl->nmuxes; i++ ) {
			if ( NULL != pctrl->muxes[ i ].groups ) {
				dev_dbg( dev, "calling 'kfree( pctrl->muxes[ %u ].groups )'\n", i );
				kfree( pctrl->muxes[ i ].groups );
				pctrl->muxes[ i ].groups = NULL;
				pctrl->muxes[ i ].ngroups = 0;
			}
		}
		dev_dbg( dev, "calling 'kfree( pctrl->muxes )'\n" );
		kfree( pctrl->muxes );
		pctrl->muxes = NULL;
		pctrl->nmuxes = 0;
	}

	if ( pctrl->ngroups > 0 && NULL != pctrl->groups ) {
		dev_dbg( dev, "freeing %u groups\n", pctrl->ngroups );
		for( i = 0; i < pctrl->ngroups; i++ ) {
			dev_dbg( dev, "might free pctrl->groups[ %u ].pins'\n", i );
			if ( pctrl->groups[ i ].npins > 0 && NULL != pctrl->groups[ i ].pins ) {
				dev_dbg( dev, "calling 'kfree( pctrl->groups[ %u ].pins )'\n", i );
				kfree( (void *) pctrl->groups[ i ].pins );
				pctrl->groups[ i ].pins = NULL;
				pctrl->groups[ i ].npins = 0;
			}
		}
		dev_dbg( dev, "calling 'kfree( pctrl->groups )'\n" );
		kfree( pctrl->groups );
		pctrl->groups = NULL;
		pctrl->ngroups = 0;
	}

	if ( pctrl->pctldesc.npins > 0 && NULL != pctrl->pctldesc.pins ) {
		dev_dbg( dev, "calling 'kfree( pctrl->pctldesc.pins )'\n" );
		kfree( (void *) pctrl->pctldesc.pins );
		pctrl->pctldesc.pins = NULL;
		pctrl->pctldesc.npins = 0;
	}

	dev_dbg( dev, "calling 'kfree( pctrl )'\n" );
	kfree( pctrl );
	pctrl = NULL;

	*ppctrl = NULL;

out:
	return;
}

/*
 * Example from Documentation/pinctrl.txt
 *
 *        A   B   C   D   E   F   G   H
 *      +---+
 *   8  | o | o   o   o   o   o   o   o
 *      |   |
 *   7  | o | o   o   o   o   o   o   o
 *      |   |
 *   6  | o | o   o   o   o   o   o   o
 *      +---+---+
 *   5  | o | o | o   o   o   o   o   o
 *      +---+---+               +---+
 *   4    o   o   o   o   o   o | o | o
 *                              |   |
 *   3    o   o   o   o   o   o | o | o
 *                              |   |
 *   2    o   o   o   o   o   o | o | o
 *      +-------+-------+-------+---+---+
 *   1  | o   o | o   o | o   o | o | o |
 *      +-------+-------+-------+---+---+
 */

static int pinctrl_fake_get_groups_count(struct pinctrl_dev *pctldev)
{
	int r;
	struct pinctrl_fake *pctrl;
	struct device *dev;

	pctrl = pinctrl_dev_get_drvdata( pctldev );
	dev = pctrl->dev;

	dev_info( dev, "In %s()\n", __FUNCTION__ );

	r = pctrl->ngroups;

	return r;
}

static const char *pinctrl_fake_get_group_name(struct pinctrl_dev *pctldev,
				      unsigned group)
{
	const char *r;
	struct pinctrl_fake *pctrl;
	struct device *dev;

	pctrl = pinctrl_dev_get_drvdata( pctldev );
	dev = pctrl->dev;

	dev_info( dev, "In %s()\n", __FUNCTION__ );

	if ( group >= pctrl->ngroups ) {
		r = NULL;
		goto out;
	}

	r = pctrl->groups[ group ].name;

out:
	return r;
}

static int pinctrl_fake_get_group_pins(struct pinctrl_dev *pctldev, unsigned group,
			      const unsigned **pins, unsigned *npins)
{
	int r;
	struct pinctrl_fake *pctrl;
	struct device *dev;

	pctrl = pinctrl_dev_get_drvdata( pctldev );
	dev = pctrl->dev;

	dev_info( dev, "In %s()\n", __FUNCTION__ );

	if ( group >= pctrl->ngroups ) {
		r = -EINVAL;
		goto out;
	}

	*pins = pctrl->groups[ group ].pins;
	*npins = pctrl->groups[ group ].npins;

	r = EXIT_SUCCESS;

out:
	return r;
}

static void pinctrl_fake_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
			     unsigned offset)
{
	struct pinctrl_fake *pctrl;
	struct device *dev;

	pctrl = pinctrl_dev_get_drvdata( pctldev );
	dev = pctrl->dev;

	dev_info( dev, "In %s()\n", __FUNCTION__ );

	// see pinctrl-intel.c for example
	return;
}

static int pinctrl_fake_get_functions_count(struct pinctrl_dev *pctldev)
{
	int r;
	struct pinctrl_fake *pctrl;
	struct device *dev;

	pctrl = pinctrl_dev_get_drvdata( pctldev );
	dev = pctrl->dev;

	dev_info( dev, "In %s()\n", __FUNCTION__ );

	r = pctrl->nmuxes;

	return r;
}

static const char *pinctrl_fake_get_function_name(struct pinctrl_dev *pctldev,
					 unsigned function)
{
	const char *r;
	struct pinctrl_fake *pctrl;
	struct device *dev;

	pctrl = pinctrl_dev_get_drvdata( pctldev );
	dev = pctrl->dev;

	dev_info( dev, "In %s()\n", __FUNCTION__ );

	if ( function >= pctrl->nmuxes ) {
		r = NULL;
		goto out;
	}

	r = pctrl->muxes[ function ].name;

out:
	return r;
}

static int pinctrl_fake_get_function_groups(struct pinctrl_dev *pctldev,
				   unsigned function,
				   const char *const **groups,
				   unsigned *const ngroups)
{
	int r;
	struct pinctrl_fake *pctrl;
	struct device *dev;

	pctrl = pinctrl_dev_get_drvdata( pctldev );
	dev = pctrl->dev;

	dev_info( dev, "In %s()\n", __FUNCTION__ );

	if ( function >= pctrl->nmuxes ) {
		r = -EINVAL;
		goto out;
	}

	*groups = (const char *const *)pctrl->muxes[ function ].groups;
	*ngroups = (unsigned const) pctrl->muxes[ function ].ngroups;
	r = EXIT_SUCCESS;

out:
	return r;
}

static int pinctrl_fake_pinmux_set_mux(struct pinctrl_dev *pctldev, unsigned function,
			      unsigned group)
{
	struct pinctrl_fake *pctrl;
	struct device *dev;

	pctrl = pinctrl_dev_get_drvdata( pctldev );
	dev = pctrl->dev;

	dev_info( dev, "In %s()\n", __FUNCTION__ );

	// see pinctrl-intel.c for example
	return EXIT_SUCCESS;
}

static int pinctrl_fake_config_get(struct pinctrl_dev *pctldev, unsigned pin,
			  unsigned long *config)
{

	struct pinctrl_fake *pctrl;
	struct device *dev;

	pctrl = pinctrl_dev_get_drvdata( pctldev );
	dev = pctrl->dev;

	dev_info( dev, "In %s()\n", __FUNCTION__ );

	// see pinctrl-intel.c for example
	return EXIT_SUCCESS;
}

static int pinctrl_fake_config_set(struct pinctrl_dev *pctldev, unsigned pin,
			  unsigned long *configs, unsigned nconfigs)
{
	struct pinctrl_fake *pctrl;
	struct device *dev;

	pctrl = pinctrl_dev_get_drvdata( pctldev );
	dev = pctrl->dev;

	dev_info( dev, "In %s()\n", __FUNCTION__ );

	// see pinctrl-intel.c for example
	return EXIT_SUCCESS;
}

static struct of_device_id pinctrl_fake_dt_ids[] = {
	{
		.compatible = "pinctrl-fake",
	},
	{
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, pinctrl_fake_dt_ids);

static struct pinctrl_fake pinctrl_fake_list_head;

static int pinctrl_fake_probe(struct platform_device *pdev)
{
	int r;

	struct pinctrl_fake *pctrl;
	struct device *dev;

	struct device_node *np;

	dev = &pdev->dev;

    if ( NULL == of_match_device( pinctrl_fake_dt_ids, dev ) ) {
		r = -ENODEV;
		goto out;
    }

	np = pdev->dev.of_node;

	pctrl = pinctrl_fake_allocate_from_dt( dev, np );
	if ( IS_ERR( pctrl ) ) {
		r = PTR_ERR( pctrl );
		pctrl = NULL;
		dev_err( dev, "pinctrl_fake_allocate_pinctrl_from_dt() failed with %d\n", r );
		goto out;
	}

	dev_info( dev, "pinctrl_fake_allocate_pinctrl_from_dt() was successful!\n" );

//	raw_spin_lock_init( & pctrl->lock );
	pctrl->dev = dev;
	platform_set_drvdata( pdev, pctrl );

	dev_info( dev, "adding to list..\n" );

	INIT_LIST_HEAD( & pctrl->head );
	list_add( & pctrl->head, & pinctrl_fake_list_head.head );

	dev_info( dev, "calling pinctrl_register()\n" );

	pctrl->pctldev = pinctrl_register( & pctrl->pctldesc, & pdev->dev, pctrl );
	if ( IS_ERR( pctrl->pctldev ) ) {
		r = PTR_ERR( pctrl->pctldev );
		dev_err( dev, "failed to register pinctrl driver (%d)\n", r );
		goto remove_from_list;
	}

	r = pinctrl_register_mappings( (struct pinctrl_map const *) pctrl->mappings, pctrl->nmappings );
	if ( EXIT_SUCCESS != r ) {
		dev_err( dev, "failed to register pinctrl mappings (%d)\n", r );
		goto unregister_pctrl;
	}

	r = of_platform_populate( dev->of_node, NULL, NULL, dev );
	if ( EXIT_SUCCESS != r ) {
		dev_err( dev, "failed to populate platform devices (%d)\n", r );
		goto unregister_map;
	}

	dev_info( dev, "Added pinctrl_fake @ %p, pdev @ %p, dev @ %p\n", pctrl, pdev, dev );

	r = EXIT_SUCCESS;
	goto out;

unregister_map:
	// FIXME: this function does not exist yet, but it needs to be implemented (1)
	// pinctrl_unregister_mappings( pctrl->mappings );

unregister_pctrl:
	pinctrl_unregister( pctrl->pctldev );
	pctrl->pctldev = NULL;

remove_from_list:
	list_del( & pctrl->head );

// free_pctrl:
	pinctrl_fake_free( dev, & pctrl );

out:
	return r;
}

static int pinctrl_fake_remove( struct platform_device *pdev )
{
	int r;
	struct pinctrl_fake *pctrl;
	struct device *dev;

	dev = & pdev->dev;
	r = -ENODEV;

	list_for_each_entry( pctrl, & pinctrl_fake_list_head.head, head ) {

		dev_dbg( dev, "evaluating pinctrl @ %p\n", pctrl );

		if ( dev == pctrl->dev ) {

			dev_info( dev, "Removing pinctrl_fake @ %p, pdev @ %p, dev @ %p\n", pctrl, pdev, dev );
			list_del( & pctrl->head );

			// dev_dbg( dev, "Calling pinctrl_unregister_mappings( %p, %u )\n", pctrl->mappings, pctrl->nmappings );
			// FIXME: this function does not exist yet, but it needs to be implemented (1)
			// pinctrl_unregister_mappings( pctrl->mappings, pctrl->nmappings );

			dev_dbg( dev, "Calling pinctrl_unregister( %p )\n", pctrl->pctldev );
			pinctrl_unregister( pctrl->pctldev );

			dev_dbg( dev, "Setting pctldesc pins to NULL and npins to 0\n" );
			// pinctrl_unregister frees the pins associated with the device but leaves pointers dangling
			pctrl->pctldesc.pins = NULL;
			pctrl->pctldesc.npins = 0;

			pinctrl_fake_free( dev, & pctrl );

			r = EXIT_SUCCESS;
			break;
		}
	}

	return r;
}

#ifdef CONFIG_PM_SLEEP
static int pinctrl_fake_suspend(struct device *dev)
{
	return 0;
}

static int pinctrl_fake_resume(struct device *dev)
{
	return 0;
}
#endif

static const struct dev_pm_ops pinctrl_fake_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pinctrl_fake_suspend, pinctrl_fake_resume)
};

static struct platform_driver pinctrl_fake_driver = {
	.probe = pinctrl_fake_probe,
	.remove = pinctrl_fake_remove,
	.driver = {
		.name = "pinctrl-fake",
		.owner = THIS_MODULE,
		.pm = &pinctrl_fake_pm_ops,
		.of_match_table = pinctrl_fake_dt_ids,
	},
};

static int __init pinctrl_fake_init( void )
{
	int r;

	INIT_LIST_HEAD( & pinctrl_fake_list_head.head );

	_pr_info( "Copyright (c) 2016, Christopher Friedt\n" );

	r = platform_driver_register( & pinctrl_fake_driver );
	if ( EXIT_SUCCESS != r ) {
		_pr_err( "platform_driver_register() failed (%d)\n", r );
		goto out;
	}

out:
	return r;
}
module_init( pinctrl_fake_init );

static void __exit pinctrl_fake_exit( void )
{
	struct pinctrl_fake *pctrl;
	struct platform_device *pdev;
	int r;

	_pr_info( "%s()..\n", __FUNCTION__ );

	for( ; ! list_empty( & pinctrl_fake_list_head.head ); ) {

		_pr_dbg( "calling list_first_entry()..\n" );

		pctrl = list_first_entry( & pinctrl_fake_list_head.head, struct pinctrl_fake, head );

		_pr_dbg( "pctrl = %p\n", pctrl );

		pdev = container_of( pctrl->dev, struct platform_device, dev );

		_pr_dbg( "pdev = %p\n", pdev );

		r = pinctrl_fake_remove( pdev );
		if ( EXIT_SUCCESS != r ) {
			_pr_err( "Unable to remove platform device @ %p\n", pdev );
		}

		_pr_dbg( "Removed pdev %p\n", pdev );
	}

	BUG_ON( ! list_empty( & pinctrl_fake_list_head.head ) );

	_pr_info( "Unregistering..\n" );

	platform_driver_unregister( & pinctrl_fake_driver );

	_pr_info( "Unloading..\n" );
}
module_exit( pinctrl_fake_exit );

MODULE_AUTHOR( "Christopher Friedt <chrisfriedt@gmail.com>" );
MODULE_DESCRIPTION( MODULE_DESC );
MODULE_LICENSE( "GPL v2" );

/*
(1)
# cat /sys/kernel/debug/pinctrl/pinctrl-maps
Pinctrl maps:
device spi-fake.0
state pos-A
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group spi0_0
function spi0

device spi-fake.0
state pos-B
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group spi0_1
function spi0

device i2c-fake.0
state i2c0
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group i2c0
function i2c0

device mmc-fake.0
state 2bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_0
function mmc0

device mmc-fake.0
state 4bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_1
function mmc0

device mmc-fake.0
state 8bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_2
function mmc0

device spi-fake.0
state pos-A
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group spi0_0
function spi0

device spi-fake.0
state pos-B
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group spi0_1
function spi0

device i2c-fake.0
state i2c0
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group i2c0
function i2c0

device mmc-fake.0
state 2bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_0
function mmc0

device mmc-fake.0
state 4bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_1
function mmc0

device mmc-fake.0
state 8bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_2
function mmc0

device spi-fake.0
state pos-A
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group spi0_0
function spi0

device spi-fake.0
state pos-B
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group spi0_1
function spi0

device i2c-fake.0
state i2c0
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group i2c0
function i2c0

device mmc-fake.0
state 2bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_0
function mmc0

device mmc-fake.0
state 4bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_1
function mmc0

device mmc-fake.0
state 8bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_2
function mmc0

device spi-fake.0
state pos-A
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group spi0_0
function spi0

device spi-fake.0
state pos-B
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group spi0_1
function spi0

device i2c-fake.0
state i2c0
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group i2c0
function i2c0

device mmc-fake.0
state 2bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_0
function mmc0

device mmc-fake.0
state 4bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_1
function mmc0

device mmc-fake.0
state 8bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_2
function mmc0

device spi-fake.0
state pos-A
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group spi0_0
function spi0

device spi-fake.0
state pos-B
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group spi0_1
function spi0

device i2c-fake.0
state i2c0
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group i2c0
function i2c0

device mmc-fake.0
state 2bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_0
function mmc0

device mmc-fake.0
state 4bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_1
function mmc0

device mmc-fake.0
state 8bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_2
function mmc0

device spi-fake.0
state pos-A
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group spi0_0
function spi0

device spi-fake.0
state pos-B
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group spi0_1
function spi0

device i2c-fake.0
state i2c0
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group i2c0
function i2c0

device mmc-fake.0
state 2bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_0
function mmc0

device mmc-fake.0
state 4bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_1
function mmc0

device mmc-fake.0
state 8bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_2
function mmc0

device spi-fake.0
state pos-A
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group spi0_0
function spi0

device spi-fake.0
state pos-B
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group spi0_1
function spi0

device i2c-fake.0
state i2c0
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group i2c0
function i2c0

device mmc-fake.0
state 2bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_0
function mmc0

device mmc-fake.0
state 4bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_1
function mmc0

device mmc-fake.0
state 8bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_2
function mmc0

device spi-fake.0
state pos-A
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group spi0_0
function spi0

device spi-fake.0
state pos-B
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group spi0_1
function spi0

device i2c-fake.0
state i2c0
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group i2c0
function i2c0

device mmc-fake.0
state 2bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_0
function mmc0

device mmc-fake.0
state 4bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_1
function mmc0

device mmc-fake.0
state 8bit
type MUX_GROUP (2)
controlling device pinctrl-fake@0
group mmc0_2
function mmc0
 */

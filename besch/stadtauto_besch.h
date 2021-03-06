/*
 *  Copyright (c) 1997 - 2002 by Volker Meyer & Hansj�rg Malthaner
 *
 * This file is part of the Simutrans project under the artistic licence.
 */

#ifndef __STADTAUTO_BESCH_H
#define __STADTAUTO_BESCH_H

#include "obj_besch_std_name.h"
#include "bildliste_besch.h"
#include "../dataobj/ribi.h"
#include "../simtypes.h"
#include "../network/checksum.h"


/*
 *  Author:
 *      Volker Meyer
 *
 *  Description:
 *	Automatisch generierte Autos, die in der Stadt umherfahren.
 *
 *  Child nodes:
 *	0   Name
 *	1   Copyright
 *	2   Image-list
 */
class stadtauto_besch_t : public obj_besch_timelined_t {
	friend class citycar_reader_t;

	uint16 gewichtung;

	/// topspeed in internal speed units !!! not km/h!!!
	uint16 geschw;

public:
	image_id get_image_id(ribi_t::dir dir) const
	{
		image_t const* const image = get_child<image_list_t>(2)->get_image(dir);
		return image != NULL ? image->get_id() : IMG_EMPTY;
	}

	uint16 get_gewichtung() const { return gewichtung; }

	/// topspeed in internal speed units !!! not km/h!!!
	uint16 get_geschw() const { return geschw; }

	void calc_checksum(checksum_t *chk) const
	{
		obj_besch_timelined_t::calc_checksum(chk);
		chk->input(gewichtung);
		chk->input(geschw);
	}
};

#endif

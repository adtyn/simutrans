/*
 * Copyright (c) 1997 - 2001 Hansj�rg Malthaner
 *
 * This file is part of the Simutrans project under the artistic licence.
 * (see licence.txt)
 */

#include <stdio.h>

#include "../simdebug.h"

#include "tunnelbauer.h"

#include "../gui/karte.h"

#include "../simworld.h"
#include "../player/simplay.h"
#include "../simtool.h"

#include "../besch/tunnel_besch.h"

#include "../boden/tunnelboden.h"

#include "../dataobj/scenario.h"
#include "../dataobj/environment.h"
#include "../dataobj/marker.h"

#include "../obj/tunnel.h"
#include "../obj/leitung2.h"
#include "../obj/signal.h"

#include "../gui/messagebox.h"
#include "../gui/tool_selector.h"

#include "wegbauer.h"
#include "../tpl/stringhashtable_tpl.h"
#include "../tpl/vector_tpl.h"


karte_ptr_t tunnelbauer_t::welt;

static stringhashtable_tpl<tunnel_besch_t *> tunnel_by_name;


void tunnelbauer_t::register_besch(tunnel_besch_t *besch)
{
	// avoid duplicates with same name
	if( const tunnel_besch_t *old_besch = tunnel_by_name.get(besch->get_name()) ) {
		dbg->warning( "tunnelbauer_t::register_besch()", "Object %s was overlaid by addon!", besch->get_name() );
		tunnel_by_name.remove(besch->get_name());
		tool_t::general_tool.remove( old_besch->get_builder() );
		delete old_besch->get_builder();
		delete old_besch;
	}
	// add the tool
	tool_build_tunnel_t *tool = new tool_build_tunnel_t();
	tool->set_icon( besch->get_cursor()->get_image_id(1) );
	tool->cursor = besch->get_cursor()->get_image_id(0);
	tool->set_default_param( besch->get_name() );
	tool_t::general_tool.append( tool );
	besch->set_builder( tool );
	tunnel_by_name.put(besch->get_name(), besch);
}


const tunnel_besch_t *tunnelbauer_t::get_besch(const char *name)
{
	return (name ? tunnel_by_name.get(name) : NULL);
}


/**
 * Find a matching tunnel
 * @author Hj. Malthaner
 */
const tunnel_besch_t *tunnelbauer_t::find_tunnel(const waytype_t wtyp, const sint32 min_speed, const uint16 time)
{
	const tunnel_besch_t *find_besch = NULL;

	FOR(stringhashtable_tpl<tunnel_besch_t*>, const& i, tunnel_by_name) {
		tunnel_besch_t* const besch = i.value;
		if(  besch->get_waytype()==wtyp  ) {
			if(  besch->is_available(time)  ) {
				if(  find_besch==NULL  ||
					(find_besch->get_topspeed()<min_speed  &&  find_besch->get_topspeed()<besch->get_topspeed())  ||
					(besch->get_topspeed()>=min_speed  &&  besch->get_wartung()<find_besch->get_wartung())
				) {
					find_besch = besch;
				}
			}
		}
	}
	return find_besch;
}


static bool compare_tunnels(const tunnel_besch_t* a, const tunnel_besch_t* b)
{
	int cmp = a->get_topspeed() - b->get_topspeed();
	if(cmp==0) {
		cmp = (int)a->get_intro_year_month() - (int)b->get_intro_year_month();
	}
	if(cmp==0) {
		cmp = strcmp(a->get_name(), b->get_name());
	}
	return cmp<0;
}


/**
 * Fill menu with icons of given waytype
 * @author Hj. Malthaner
 */
void tunnelbauer_t::fill_menu(tool_selector_t* tool_selector, const waytype_t wtyp, sint16 /*sound_ok*/)
{
	// check if scenario forbids this
	if (!welt->get_scenario()->is_tool_allowed(welt->get_active_player(), TOOL_BUILD_TUNNEL | GENERAL_TOOL, wtyp)) {
		return;
	}

	const uint16 time=welt->get_timeline_year_month();
	vector_tpl<const tunnel_besch_t*> matching(tunnel_by_name.get_count());

	FOR(stringhashtable_tpl<tunnel_besch_t*>, const& i, tunnel_by_name) {
		tunnel_besch_t* const besch = i.value;
		if(  besch->get_waytype()==wtyp  &&  besch->is_available(time)  ) {
			matching.insert_ordered(besch, compare_tunnels);
		}
	}
	// now sorted ...
	FOR(vector_tpl<tunnel_besch_t const*>, const i, matching) {
		tool_selector->add_tool_selector(i->get_builder());
	}
}


const vector_tpl<const tunnel_besch_t *>& tunnelbauer_t::get_available_tunnels(const waytype_t wtyp)
{
	static vector_tpl<const tunnel_besch_t *> dummy;
	dummy.clear();
	const uint16 time = welt->get_timeline_year_month();
	FOR(stringhashtable_tpl<tunnel_besch_t*>, const& i, tunnel_by_name) {
		tunnel_besch_t const* const b = i.value;
		if (  b->get_waytype()==wtyp  &&  b->is_available(time)  ) {
			dummy.append(b);
		}
	}
	return dummy;
}


/* now construction stuff */


koord3d tunnelbauer_t::finde_ende(player_t *player, koord3d pos, koord zv, const tunnel_besch_t *besch, bool full_tunnel, const char** msg)
{
	const grund_t *gr;
	leitung_t *lt;
	waytype_t wegtyp = besch->get_waytype();
	// use the is_allowed_step routine of wegbauer_t, needs an instance
	wegbauer_t bauigel(player);
	bauigel.route_fuer( wegbauer_t::tunnel_flag | (wegbauer_t::bautyp_t)wegtyp, wegbauer_t::weg_search( wegtyp, 1, 0, type_flat ), besch);
	sint32 dummy;

	while(true) {
		pos = pos + zv;
		if(!welt->is_within_limits(pos.get_2d())) {
			return koord3d::invalid;
		}

		// check if ground is below tunnel level
		gr = welt->lookup_kartenboden(pos.get_2d());

		// steep slopes and we are appearing at the top of one
		if(  gr->get_hoehe() == pos.z-1  &&  env_t::pak_height_conversion_factor==1  ) {
			const slope_t::type new_slope = slope_type(-zv);
			sint8 hsw = pos.z + corner_sw(new_slope);
			sint8 hse = pos.z + corner_se(new_slope);
			sint8 hne = pos.z + corner_ne(new_slope);
			sint8 hnw = pos.z + corner_nw(new_slope);
			karte_t::terraformer_t raise(welt);
			raise.add_raise_node(pos.x, pos.y, hsw, hse, hne, hnw);
			raise.iterate(true);
			if (raise.can_raise_all(player)) {
				// returned true therefore error reported
				return koord3d::invalid;
			}
			// if we can adjust height here we can build an entrance so don't need checks below
			return pos;
		}
		if(  gr->get_hoehe() < pos.z  ){
			return koord3d::invalid;
		}

		// check water level
		if (gr->ist_wasser()  &&  welt->lookup_hgt(pos.get_2d()) <= pos.z) {
			return koord3d::invalid;
		}

		if (const char* err = welt->get_scenario()->is_work_allowed_here(player, TOOL_BUILD_TUNNEL|GENERAL_TOOL, wegtyp, pos)) {
			if (msg) {
				*msg = err;
			}
			return koord3d::invalid;
		}

		// next tile
		gr = welt->lookup(pos);
		if(  gr == NULL  ) {
			// check for slope down ...
			gr = welt->lookup(pos + koord3d(0,0,-1));
 			if(  !gr  ) {
				gr = welt->lookup(pos + koord3d(0,0,-2));
			}
			if(  gr  &&  gr->get_weg_hang() == slope_t::flat  ) {
				// Don't care about _flat_ tunnels below.
				gr = NULL;
			}

			if(  !gr  &&  env_t::pak_height_conversion_factor==2  ) {
				// check for one above
				gr = welt->lookup(pos + koord3d(0,0,1));
			}
		}

		if(gr) {
			// if there is a tunnel try to connect
			if(  gr->ist_tunnel() ) {
				if(  gr->get_vmove(ribi_type(-zv))!=pos.z) {
					// wrong slope
					return koord3d::invalid;
				}
				// fake tunnel tile
				tunnelboden_t from(pos - zv, slope_t::flat);
				if (bauigel.is_allowed_step(&from, gr, &dummy)) {
					return gr->get_pos();
				}
				else {
					return koord3d::invalid;
				}
			}
			const uint8 slope = gr->get_grund_hang();
			const slope_t::type new_slope = slope_type(-zv) * welt->get_settings().get_way_height_clearance();

			if(  gr->ist_karten_boden()  &&  ( slope!=new_slope  ||  pos.z!=gr->get_pos().z )  ) {
				// lower terrain to match - most of time shouldn't need to raise
				// however player might have manually altered terrain so check this anyway
				sint8 hsw = pos.z + corner_sw(new_slope);
				sint8 hse = pos.z + corner_se(new_slope);
				sint8 hne = pos.z + corner_ne(new_slope);
				sint8 hnw = pos.z + corner_nw(new_slope);
				karte_t::terraformer_t raise(welt), lower(welt);
				raise.add_raise_node(pos.x, pos.y, hsw, hse, hne, hnw);
				raise.iterate(false);
				lower.add_lower_node(pos.x, pos.y, hsw, hse, hne, hnw);
				lower.iterate(false);
				if (raise.can_lower_all(player) || lower.can_lower_all(player)) {
					// returned true therefore error reported
					return koord3d::invalid;
				}
				// if we can adjust height here we can build an entrance so don't need checks below
				return pos;
			}


			if(  gr->get_typ() != grund_t::boden  ||  slope != new_slope  ||  gr->is_halt()  ||  ((wegtyp != powerline_wt) ? gr->get_leitung() != NULL : gr->hat_wege())  ) {
				// must end on boden_t and correct slope and not on halts
				// ways cannot end on powerlines, powerlines cannot end on ways
				return koord3d::invalid;
			}
			if(  gr->has_two_ways()  &&  wegtyp != road_wt  ) {
				// Only road tunnels allowed here.
				return koord3d::invalid;
			}

			ribi_t::ribi ribi = 0;
			if(wegtyp != powerline_wt) {
				ribi = gr->get_weg_ribi_unmasked(wegtyp);
			}
			else {
				if(gr->get_leitung()) {
					ribi = gr->get_leitung()->get_ribi();
				}
			}

			if(  ribi && koord(ribi) == zv  ) {
				// There is already a way (with correct ribi)
				return pos;
			}
			if(  !ribi  ) {
				// End of the slope - Missing end rail or has no ribis
				// we still consider if we interfere with a way (original: pr�fen noch, ob uns dort ein anderer Weg st�rt)
				if(wegtyp != powerline_wt) {
					if(  !gr->hat_wege()  ||  gr->hat_weg(wegtyp)  ) {
						return pos;
					}
				}
				else {
					lt = gr->find<leitung_t>();
					if(!gr->hat_wege() || lt) {
						return pos;
					}
				}
			}
			return koord3d::invalid;  // Was im Weg (slope hillside or so)
		}

		// stop if we only want to check tile behind tunnel mouth
		if (!full_tunnel) {
			return pos;
		}
		// All free - keep looking
	}
}


const char *tunnelbauer_t::baue( player_t *player, koord pos, const tunnel_besch_t *besch, bool full_tunnel )
{
	assert( besch );

	const grund_t *gr = welt->lookup_kartenboden(pos);
	if(gr==NULL) {
		return "Tunnel must start on single way!";
	}

	koord zv;
	const waytype_t wegtyp = besch->get_waytype();
	const slope_t::type slope = gr->get_grund_hang();

	if(  wegtyp != powerline_wt  ) {
		const weg_t *weg = gr->get_weg(wegtyp);

		if(  gr->get_typ() != grund_t::boden  ||  gr->is_halt()  ||  gr->get_leitung()) {
			return "Tunnel must start on single way!";
		}
		// If there is a way on this tile, it must have the right ribis.
		if(  weg  &&  (weg->get_ribi_unmasked() & ~ribi_t::backward( ribi_type(slope) ))  ) {
			return "Tunnel must start on single way!";
		}
	}
	else {
		leitung_t *lt = gr->find<leitung_t>();
		if(  gr->get_typ() != grund_t::boden  ||  gr->hat_wege()  ) {
			return "Tunnel must start on single way!";
		}
		if(  lt  &&  (lt->get_ribi() & ~ribi_t::backward( ribi_type(slope) ))  ) {
			return "Tunnel must start on single way!";
		}
	}
	if(  !slope_t::is_single(slope)  ) {
		return "Tunnel muss an\neinfachem\nHang beginnen!\n";
	}

/************************************** FIX ME ***************************************************
********************** THIS MUST BE RATHER A PROPERTY OF THE TUNNEL IN QUESTION ! ****************/
	// for conversion factor 1, must be single height, for conversion factor 2, must be double
	if(  (env_t::pak_height_conversion_factor == 1  &&  !(slope & 7))  ||  (env_t::pak_height_conversion_factor == 2  &&  (slope & 7))  ) {
		return "Tunnel muss an\neinfachem\nHang beginnen!\n";
	}

	if(  gr->has_two_ways()  &&  wegtyp != road_wt  ) {
		return "Tunnel must start on single way!";
	}
	zv = koord(slope);

	// Search tunnel end and check intermediate tiles
	const char *err = NULL;
	koord3d end = finde_ende(player, gr->get_pos(), zv, besch, full_tunnel, &err);
	if (err) {
		return err;
	}

	if(!welt->is_within_limits(end.get_2d())) {
		return "Tunnel must start on single way!";
	}

	// check ownership
	if (const grund_t *gr_end = welt->lookup(end)) {
		if (weg_t *weg_end = gr_end->get_weg(wegtyp)) {
			if (weg_end->is_deletable(player)!=NULL) {
				return "Das Feld gehoert\neinem anderen Spieler\n";
			}
		}
	}

	// Begin and end found, we can build

	const grund_t *end_gr = welt->lookup(end);
	slope_t::type end_slope = slope_type(-zv) * env_t::pak_height_conversion_factor;
	if(  full_tunnel  &&  (!end_gr  ||  end_gr->get_grund_hang()!=end_slope)  ) {
		// end slope not at correct height - we have already checked in finde_ende that we can change this
		sint8 hsw = end.z + corner_sw(end_slope);
		sint8 hse = end.z + corner_se(end_slope);
		sint8 hne = end.z + corner_ne(end_slope);
		sint8 hnw = end.z + corner_nw(end_slope);

		int n = 0;

		karte_t::terraformer_t raise(welt),lower(welt);
		raise.add_raise_node(end.x, end.y, hsw, hse, hne, hnw);
		lower.add_lower_node(end.x, end.y, hsw, hse, hne, hnw);
		raise.iterate(true);
		lower.iterate(false);
		err = raise.can_raise_all(player);
		if (!err) err = lower.can_lower_all(player);
		if (err) return 0;

// TODO: this is rather hackish as 4 seems to come from nowhere but works most of the time
// feel free to change if you have a better idea!
		n = (raise.raise_all()+lower.lower_all())/4;
		player_t::book_construction_costs(player, welt->get_settings().cst_alter_land * n, end.get_2d(), besch->get_waytype());
	}

	if(!baue_tunnel(player, gr->get_pos(), end, zv, besch)) {
		return "Ways not connected";
	}
	return NULL;
}


bool tunnelbauer_t::baue_tunnel(player_t *player, koord3d start, koord3d end, koord zv, const tunnel_besch_t *besch)
{
	ribi_t::ribi ribi = 0;
	weg_t *weg = NULL;
	leitung_t *lt = NULL;
	koord3d pos = start;
	int cost = 0;
	const weg_besch_t *weg_besch;
	waytype_t wegtyp = besch->get_waytype();

DBG_MESSAGE("tunnelbauer_t::baue()","build from (%d,%d,%d) to (%d,%d,%d) ", pos.x, pos.y, pos.z, end.x, end.y, end.z );

	// now we search a matching way for the tunnels top speed
	weg_besch = besch->get_weg_besch();
	if(weg_besch==NULL) {
		// ignore timeline to get consistent results
		weg_besch = wegbauer_t::weg_search( wegtyp, besch->get_topspeed(), 0, type_flat );
	}

	baue_einfahrt(player, pos, zv, besch, weg_besch, cost);

	ribi = ribi_type(-zv);
	// don't move on to next tile if only one tile long
	if(  end != start  ) {
		pos = pos + zv;
	}
	// calc new back image for the ground
	if(grund_t::underground_mode) {
		grund_t *gr = welt->lookup_kartenboden(pos.get_2d());
		gr->calc_image();
		gr->set_flag(grund_t::dirty);
	}

	// Now we build the invisible part
	while(pos.get_2d()!=end.get_2d()) {
		tunnelboden_t *tunnel = new tunnelboden_t( pos, 0);
		welt->access(pos.get_2d())->boden_hinzufuegen(tunnel);
		if(wegtyp != powerline_wt) {
			weg = weg_t::alloc(besch->get_waytype());
			weg->set_besch(weg_besch);
			weg->set_max_speed(besch->get_topspeed());
			tunnel->neuen_weg_bauen(weg, ribi_t::doubles(ribi), player);
			player_t::add_maintenance( player, -weg->get_besch()->get_wartung(), weg->get_besch()->get_finance_waytype() );
		}
		else {
			lt = new leitung_t(tunnel->get_pos(), player);
			lt->set_besch(weg_besch);
			tunnel->obj_add( lt );
			lt->finish_rd();
			player_t::add_maintenance( player, -weg_besch->get_wartung(), powerline_wt );
		}
		tunnel->obj_add(new tunnel_t(pos, player, besch));
		tunnel->calc_image();
		tunnel->set_flag(grund_t::dirty);
		assert(!tunnel->ist_karten_boden());
		player_t::add_maintenance( player, besch->get_wartung(), besch->get_finance_waytype() );
		cost += besch->get_preis();
		pos = pos + zv;
	}

	// if end is tunnel then connect
	grund_t *gr_end = welt->lookup(end);
	if (gr_end) {
		if (gr_end->ist_tunnel()) {
			gr_end->weg_erweitern(besch->get_waytype(), ribi);
		}
		else if (gr_end->ist_karten_boden()) {
			// if end is above ground construct an exit
			baue_einfahrt(player, pos, -zv, besch, weg_besch, cost);
			gr_end = NULL; // invalid - replaced by tunnel ground
			// calc new back image for the ground
			if (end!=start && grund_t::underground_mode) {
				grund_t *gr = welt->lookup_kartenboden(pos.get_2d()-zv);
				gr->calc_image();
				gr->set_flag(grund_t::dirty);
			}
		}
		else {
			// good luck
			assert(0);
		}
	}
	else {
		// construct end tunnel tile
		tunnelboden_t *tunnel = new tunnelboden_t( pos, 0);
		welt->access(pos.get_2d())->boden_hinzufuegen(tunnel);
		if(wegtyp != powerline_wt) {
			weg = weg_t::alloc(besch->get_waytype());
			weg->set_besch(weg_besch);
			weg->set_max_speed(besch->get_topspeed());
			tunnel->neuen_weg_bauen(weg, ribi, player);
			player_t::add_maintenance( player,  -weg->get_besch()->get_wartung(), weg->get_besch()->get_finance_waytype() );
		}
		else {
			lt = new leitung_t(tunnel->get_pos(), player);
			lt->set_besch(weg_besch);
			tunnel->obj_add( lt );
			lt->finish_rd();
			player_t::add_maintenance( player, -weg_besch->get_wartung(), powerline_wt );
		}
		tunnel->obj_add(new tunnel_t(pos, player, besch));
		tunnel->calc_image();
		tunnel->set_flag(grund_t::dirty);
		assert(!tunnel->ist_karten_boden());
		player_t::add_maintenance( player,  besch->get_wartung(), besch->get_finance_waytype() );
		cost += besch->get_preis();
	}

	player_t::book_construction_costs(player, -cost, start.get_2d(), besch->get_waytype());
	return true;
}


void tunnelbauer_t::baue_einfahrt(player_t *player, koord3d end, koord zv, const tunnel_besch_t *besch, const weg_besch_t *weg_besch, int &cost)
{
	grund_t *alter_boden = welt->lookup(end);
	ribi_t::ribi ribi = 0;
	if(besch->get_waytype()!=powerline_wt) {
		ribi = alter_boden->get_weg_ribi_unmasked(besch->get_waytype()) | ribi_type(zv);
	}

	tunnelboden_t *tunnel = new tunnelboden_t( end, alter_boden->get_grund_hang());
	tunnel->obj_add(new tunnel_t(end, player, besch));

	weg_t *weg = NULL;
	if(besch->get_waytype()!=powerline_wt) {
		weg = alter_boden->get_weg( besch->get_waytype() );
	}
	// take care of everything on that tile ...
	tunnel->take_obj_from( alter_boden );
	welt->access(end.get_2d())->kartenboden_setzen( tunnel );
	if(besch->get_waytype() != powerline_wt) {
		if(weg) {
			// has already a way
			tunnel->weg_erweitern(besch->get_waytype(), ribi);
		}
		else {
			// needs still one
			weg = weg_t::alloc( besch->get_waytype() );
			if(  weg_besch  ) {
				weg->set_besch( weg_besch );
			}
			tunnel->neuen_weg_bauen( weg, ribi, player );
		}
		player_t::add_maintenance( player, -weg->get_besch()->get_wartung(), weg->get_besch()->get_finance_waytype() );
		weg->set_max_speed( besch->get_topspeed() );
	}
	else {
		leitung_t *lt = tunnel->get_leitung();
		if(!lt) {
			lt = new leitung_t(tunnel->get_pos(), player);
			lt->set_besch(weg_besch);
			tunnel->obj_add( lt );
			player_t::add_maintenance( player, -weg_besch->get_wartung(), powerline_wt );
		}
		else {
			// subtract twice maintenance: once for the already existing powerline
			// once since leitung_t::finish_rd will add it again
			player_t::add_maintenance( player, -2*lt->get_besch()->get_wartung(), powerline_wt );
		}
		lt->finish_rd();
	}

	// remove sidewalk
	weg_t *str = tunnel->get_weg( road_wt );
	if( str  &&  str->hat_gehweg()) {
		str->set_gehweg(false);
	}

	tunnel->calc_image();
	tunnel->set_flag(grund_t::dirty);

	// Auto-connect to a way outside the new tunnel mouth
	grund_t *ground_outside = welt->lookup(end-zv);
	if( !ground_outside ) {
		ground_outside = welt->lookup(end-zv+koord3d(0,0,-1));
		if(  ground_outside  &&  ground_outside->get_grund_hang() != tunnel->get_grund_hang()  ) {
			// Not the correct slope.
			ground_outside = NULL;
		}
	}
	if( ground_outside) {
		weg_t *way_outside = ground_outside->get_weg( besch->get_waytype() );
		if( way_outside ) {
			// use the check_owner routine of wegbauer_t (not spieler_t!), needs an instance
			wegbauer_t bauigel(player);
			bauigel.route_fuer( (wegbauer_t::bautyp_t)besch->get_waytype(), way_outside->get_besch());
			sint32 dummy;
			if(bauigel.is_allowed_step(tunnel, ground_outside, &dummy)) {
				tunnel->weg_erweitern(besch->get_waytype(), ribi_type(-zv));
				ground_outside->weg_erweitern(besch->get_waytype(), ribi_type(zv));
			}
		}
		if (besch->get_waytype()==water_wt  &&  ground_outside->ist_wasser()) {
			// connect to the sea
			tunnel->weg_erweitern(besch->get_waytype(), ribi_type(-zv));
			ground_outside->calc_image(); // to recalculate ribis
		}
	}


	if(player!=NULL) {
		player_t::add_maintenance( player,  besch->get_wartung(), besch->get_finance_waytype() );
	}
	cost += besch->get_preis();
}


const char *tunnelbauer_t::remove(player_t *player, koord3d start, waytype_t wegtyp, bool remove_all )
{
	marker_t& marker = marker_t::instance(welt->get_size().x, welt->get_size().y);
	slist_tpl<koord3d>  end_list;
	slist_tpl<koord3d>  part_list;
	slist_tpl<koord3d>  tmp_list;
	koord3d   pos = start;

	// Erstmal das ganze Au�ma� des Tunnels bestimmen und sehen,
	// ob uns was im Weg ist.
	tmp_list.insert(pos);
	grund_t *from = welt->lookup(pos);
	marker.mark(from);
	waytype_t delete_wegtyp = wegtyp==powerline_wt ? invalid_wt : wegtyp;

	do {
		pos = tmp_list.remove_first();

		// V.Meyer: weg_position_t changed to grund_t::get_neighbour()
		grund_t *from = welt->lookup(pos);
		grund_t *to;
		koord zv = koord::invalid;

		if(from->ist_karten_boden()) {
			// Der Grund ist Tunnelanfang/-ende - hier darf nur in
			// eine Richtung getestet werden.
			zv = koord(from->get_grund_hang());
			end_list.insert(pos);
		}
		else {
			part_list.insert(pos);
		}
		// Alle Tunnelteile auf Entfernbarkeit pr�fen!
		if(  from->kann_alle_obj_entfernen(player)  ) {
			return "Der Tunnel ist nicht frei!\n";
		}

		ribi_t::ribi waytype_ribi = ribi_t::none;
		if(  wegtyp == powerline_wt  ) {
			if(  from->get_leitung()  ) {
				waytype_ribi = from->get_leitung()->get_ribi();
			}
		}
		else {
			waytype_ribi = from->get_weg_ribi_unmasked(delete_wegtyp);
		}
		if(  !remove_all  &&  ribi_t::is_threeway(waytype_ribi)  ) {
			return "This tunnel branches. You can try Control+Click to remove.";
		}

		// Nachbarn raussuchen
		for(int r = 0; r < 4; r++) {
			if((zv == koord::invalid || zv == koord::nsew[r]) &&
				from->get_neighbour(to, delete_wegtyp, ribi_t::nsew[r]) &&
				!marker.is_marked(to) &&
				(wegtyp != powerline_wt || to->get_leitung()))
			{
				tmp_list.insert(to->get_pos());
				marker.mark(to);
			}
		}
	} while (!tmp_list.empty());

	// Jetzt geht es ans l�schen der Tunnel
	while (!part_list.empty()) {
		pos = part_list.remove_first();
		grund_t *gr = welt->lookup(pos);
		// remove the second way first in the tunnel
		if(gr->get_weg_nr(1)) {
			gr->remove_everything_from_way(player,gr->get_weg_nr(1)->get_waytype(),ribi_t::none);
		}
		gr->remove_everything_from_way(player,wegtyp,ribi_t::none);	// removes stop and signals correctly
		// remove everything else
		gr->obj_loesche_alle(player);
		gr->mark_image_dirty();
		welt->access(pos.get_2d())->boden_entfernen(gr);
		delete gr;

		reliefkarte_t::get_karte()->calc_map_pixel( pos.get_2d() );
	}

	// Und die Tunnelenden am Schlu�
	while (!end_list.empty()) {
		pos = end_list.remove_first();

		grund_t *gr = welt->lookup(pos);
		if(wegtyp == powerline_wt) {
			// remove tunnel portals
			tunnel_t *t = gr->find<tunnel_t>();
			if(t) {
				t->cleanup(player);
				delete t;
			}
			if (leitung_t *lt = gr->get_leitung()) {
				// remove single powerlines
				if ( (lt->get_ribi()  & ~ribi_type(gr->get_grund_hang())) == ribi_t::none ) {
					lt->cleanup(player);
					delete lt;
				}
			}
		}
		else {
			ribi_t::ribi mask = gr->get_grund_hang()!=slope_t::flat ? ~ribi_type(gr->get_grund_hang()) : ~ribi_type(slope_t::opposite(gr->get_weg_hang()));

			// remove the second way first in the tunnel
			if(gr->get_weg_nr(1)) {
				gr->remove_everything_from_way(player,gr->get_weg_nr(1)->get_waytype(),gr->get_weg_nr(1)->get_ribi_unmasked() & mask);
			}
			// removes single signals, bridge head, pedestrians, stops, changes catenary etc
			ribi_t::ribi ribi = gr->get_weg_ribi_unmasked(wegtyp) & mask;

			tunnel_t *t = gr->find<tunnel_t>();
			uint8 broad_type = t->get_broad_type();
			gr->remove_everything_from_way(player,wegtyp,ribi);	// removes stop and signals correctly

			// remove tunnel portals
			t = gr->find<tunnel_t>();
			if(t) {
				t->cleanup(player);
				delete t;
			}

			if( broad_type ) {
				slope_t::type hang = gr->get_grund_hang();
				ribi_t::ribi dir = ribi_t::rotate90( ribi_type( hang ) );
				if( broad_type & 1 ) {
					const grund_t *gr_l = welt->lookup(pos + dir);
					tunnel_t* tunnel_l = gr_l ? gr_l->find<tunnel_t>() : NULL;
					if( tunnel_l ) {
						tunnel_l->calc_image();
					}
				}
				if( broad_type & 2 ) {
					const grund_t *gr_r = welt->lookup(pos - dir);
					tunnel_t* tunnel_r = gr_r ? gr_r->find<tunnel_t>() : NULL;
					if( tunnel_r ) {
						tunnel_r->calc_image();
					}
				}
			}

			// corrects the ways
			weg_t *weg=gr->get_weg_nr(0);
			if(weg) {
				// fails if it was previously the last ribi
				weg->set_besch(weg->get_besch());
				weg->set_ribi( ribi );
				if(gr->get_weg_nr(1)) {
					gr->get_weg_nr(1)->set_ribi( ribi );
				}
			}
		}

		// then add the new ground, copy everything and replace the old one
		grund_t *gr_new = new boden_t(pos, gr->get_grund_hang());
		gr_new->take_obj_from( gr );
		welt->access(pos.get_2d())->kartenboden_setzen(gr_new );

		if(gr_new->get_leitung()) {
			gr_new->get_leitung()->finish_rd();
		}

		// recalc image of ground
		grund_t *kb = welt->access(pos.get_2d()+koord(gr_new->get_grund_hang()))->get_kartenboden();
		kb->calc_image();
		kb->set_flag(grund_t::dirty);
	}
	return NULL;
}

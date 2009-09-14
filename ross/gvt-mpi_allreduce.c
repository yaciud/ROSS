#include <ross.h>

#define TW_GVT_NORMAL 0
#define TW_GVT_COMPUTE 1

static tw_stat tot_mattern_nochange = 0;
static unsigned int g_tw_gvt_max_no_change = 10000;
static unsigned int g_tw_gvt_no_change = 0;
static unsigned int mattern_nochange = 0;
static unsigned int gvt_cnt = 0;
static unsigned int gvt_force = 0;
static unsigned int g_tw_gvt_interval = 16;

static const tw_optdef gvt_opts [] =
{
	TWOPT_GROUP("ROSS MPI GVT"),
	TWOPT_UINT("gvt-interval", g_tw_gvt_interval, "GVT Interval"),
	TWOPT_STIME("report-interval", gvt_print_interval, 
			"percent of runtime to print GVT"),
	TWOPT_END()
};

const tw_optdef *
tw_gvt_setup(void)
{
	gvt_cnt = 0;

	return gvt_opts;
}

void
tw_gvt_start(void)
{
}

void
tw_gvt_force_update(tw_pe *me)
{
	gvt_force++;
	gvt_cnt = g_tw_gvt_interval;
}

void
tw_gvt_stats(FILE * f)
{
	fprintf(f, "TW GVT Statistics: MPI AllReduce\n");
	fprintf(f, "\t%-50s %11d\n", "GVT Interval", g_tw_gvt_interval);
	fprintf(f, "\t%-50s %11d\n", "Batch Size", g_tw_mblock);
	fprintf(f, "\n");
	fprintf(f, "\t%-50s %11d\n", "Forced GVT", gvt_force);
	fprintf(f, "\t%-50s %11d\n", "Total GVT Computations", g_tw_gvt_done);
	fprintf(f, "\t%-50s %11lld\n", "Total Reduction Attempts", 
			tot_mattern_nochange);
	fprintf(f, "\t%-50s %11.2lf\n", "Average Reduction Attempts / GVT", 
			(double) ((double) tot_mattern_nochange / (double) g_tw_gvt_done));
}

void
tw_gvt_step1(tw_pe *me)
{
	if(me->gvt_status == TW_GVT_COMPUTE ||
		++gvt_cnt < g_tw_gvt_interval)
		return;

	me->gvt_status = TW_GVT_COMPUTE;
}

void
tw_gvt_step2(tw_pe *me)
{
	tw_stat local_white = 0;
	tw_stat total_white = 0;

	tw_stime pq_min = DBL_MAX;
	tw_stime net_min = DBL_MAX;

	tw_stime lvt;
	tw_stime gvt;

	if(me->gvt_status != TW_GVT_COMPUTE)
		return;

while(1)
{
	tw_net_read(me);
	
	// send message counts to create consistent cut
	local_white = me->s_nwhite_sent - me->s_nwhite_recv;

	if(MPI_Allreduce(
		&local_white,
		&total_white,
		1,
		MPI_LONG_LONG,
		MPI_SUM,
		MPI_COMM_WORLD) != MPI_SUCCESS)
		tw_error(TW_LOC, "MPI_Allreduce for GVT failed");

	if(total_white == 0)
		break;
	else
		++mattern_nochange;
}

	pq_min = tw_pq_minimum(me->pq);
	net_min = tw_net_minimum(me);

	lvt = me->trans_msg_ts;
	if(lvt > pq_min)
		lvt = pq_min;
	if(lvt > net_min)
		lvt = net_min;

	if(MPI_Allreduce(
			&lvt,
			&gvt,
			1,
			MPI_DOUBLE,
			MPI_MIN,
			MPI_COMM_WORLD) != MPI_SUCCESS)
			tw_error(TW_LOC, "MPI_Allreduce for GVT failed");

	gvt = min(gvt, me->GVT_prev);

	if(gvt != me->GVT_prev)
	{
		g_tw_gvt_no_change = 0;
	} else
	{
		g_tw_gvt_no_change++;
		if (g_tw_gvt_no_change >= g_tw_gvt_max_no_change) {
			tw_error(
				TW_LOC,
				"GVT computed %d times in a row"
				" without changing: GVT = %14.14lf, PREV %14.14lf"
				" -- GLOBAL SYNCH -- out of memory!",
				g_tw_gvt_no_change, gvt, me->GVT_prev);
		}
	}

	if (me->GVT > gvt)
	{
		tw_error(TW_LOC, "PE %u GVT decreased %g -> %g",
				me->id, me->GVT, gvt);
	}

	if(gvt / g_tw_ts_end > percent_complete &&
		tw_node_eq(&g_tw_mynode, &g_tw_masternode))
	{
		gvt_print(gvt);
	}

	me->s_nwhite_sent = 0;
	me->s_nwhite_recv = 0;
	me->trans_msg_ts = DBL_MAX;
	me->GVT_prev = DBL_MAX; // me->GVT;
	me->GVT = gvt;
	me->gvt_status = TW_GVT_NORMAL;

	tot_mattern_nochange += mattern_nochange;
	mattern_nochange = 0;
	gvt_cnt = 0;

	tw_pe_fossil_collect(me);

	g_tw_gvt_done++;
}
/*JWHGPLHERE*/
/*
 * DistLdarProcFVar.c
 *
 * Author: James W Hegeman
 *
 */

#include "DLPstd.h"
#include "LdarReader.h"
#include "DistLdarProcFVar.h"
#include "Triangulate.h"
#include "ProcessData.h"
#include "MergeSlave.h"
#include "MergeMaster.h"
#include "DistributeSlave.h"
#include "DistributeMaster.h"

LidarPointNode_t *BinTbl[NUM_CELLS][NUM_BINS_X][NUM_BINS_Y];
double BinU1[NUM_CELLS][NUM_BINS_X][NUM_BINS_Y];
double BinU2[NUM_CELLS][NUM_BINS_X][NUM_BINS_Y];
uint32_t BinCnt[NUM_CELLS][NUM_BINS_X][NUM_BINS_Y];

double Zinit[NUM_CELLS][4];
LidarPointNode_t CellMin[NUM_CELLS];
LidarPointNode_t CellMax[NUM_CELLS];
uint32_t CellCnt[NUM_CELLS];

struct sockaddr_in svr_addr[NUM_NODES];
struct epoll_event msockevents[NUM_NODES];
int msock[NUM_NODES];

pthread_t Workers[NUM_WORKERS + 1];
int WorkerIDs[NUM_WORKERS + 1];

char NodeIPs[NUM_NODES][16];

LidarPointNode_t NodeMin;
LidarPointNode_t NodeMax;

struct timeval t_start;
struct timeval t_end;

double t_diff;
double Xdiff;
double Ydiff;
double Xratio;
double Yratio;
double Xint;
double Yint;
double Xint_cell;
double Yint_cell;
double Xint_bin;
double Yint_bin;
double X_c;
double Y_c;
double Z_c;

FILE *addrfile;
LidarPointNode_t *PntTbl;
LidarPointNode_t *current;
LidarPointNode_t *boundary_begin;
struct epoll_event *newevents;
double *Z2;
double *current2;
int8_t *FiltTbl;
void *X_b;
void *Y_b;
void *Z_b;

int32_t X;
int32_t Y;
int32_t Z;
uint32_t count;
uint32_t mycount = 0;
int NodeID;
int ssock;
int polldesc;

void *Malloc(size_t len) {
    void *ret;
    ret = malloc(len);
    if (ret == NULL) perror("malloc()");
    return ret;
}

int main(int argc, char *argv[]) {

    char ip[16];
    uint32_t little;
    uint32_t ix;
    uint32_t iy;
    INT nt;
    int i;
    int id;
    int ip1;
    int ip2;
    int ip3;
    int ip4;

    if (NUM_NODES == 1 && argc != 2 || NUM_NODES != 1 && (argc < 3 || argc > 4)) {
	fprintf(stderr, "Usage (one node)......: %s INFILE\n", argv[0]);
	fprintf(stderr, "Usage (multiple nodes): %s NODE_ID ADDRFILE [INFILE]\n", argv[0]);
	fflush(stderr);
	exit(-1);
    }

    assert(sizeof (char) == CHAR_SIZE);
    assert(sizeof (unsigned char) == UCHAR_SIZE);
    assert(sizeof (uint16_t) == UINT16_SIZE);
    assert(sizeof (int32_t) == INT32_SIZE);
    assert(sizeof (uint32_t) == UINT32_SIZE);
    assert(sizeof (double) == DOUBLE_SIZE);
    assert(NUM_BINS_X >= 2);
    assert(NUM_BINS_Y >= 2);
    assert(NUM_BINS_X % 2 == 0);
    assert(NUM_BINS_Y % 2 == 0);
    assert(NUM_NODES >= 1);
    assert(NUM_NODES_X * NUM_NODES_Y == NUM_NODES);
    assert(NUM_CELLS_X * NUM_CELLS_Y == NUM_CELLS);
    assert(NUM_WORKERS == NUM_CELLS - 1);
    little = 16976155;
    assert(*(((unsigned char *) &little) + 0) == (unsigned char) 0x1B);
    assert(*(((unsigned char *) &little) + 1) == (unsigned char) 0x09);
    assert(*(((unsigned char *) &little) + 2) == (unsigned char) 0x03);
    assert(*(((unsigned char *) &little) + 3) == (unsigned char) 0x01);

    gettimeofday(&t_start, NULL);

    if (argc == 4) {
	NodeID = (int) strtol(argv[1], NULL, 10);

	if ((addrfile = fopen(argv[2], "r")) == NULL) {
	    fprintf(stderr, "Could not open file %s for reading. Exiting.\n", argv[2]);
	    fflush(stderr);
	    exit(-1);
	}

	for (i = 0; i < NUM_NODES; ++i) {
	    fscanf(addrfile, "%d %d.%d.%d.%d", &id, &ip1, &ip2, &ip3, &ip4);
	    snprintf(ip, 16, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
	    strncpy(NodeIPs[id], ip, 16);
	}

	if (fclose(addrfile)) perror("fclose()");
    } else {
	NodeID = 0;
    }

    if (NodeID == 0) {
	if (argc == 2) {
	    LasFileInit(argv[1]);
	} else {
	    LasFileInit(argv[3]);
	}
    }

    PntTbl = (LidarPointNode_t *) Malloc(NODE_POINTS_MAX * sizeof (LidarPointNode_t));
    current = PntTbl;

    Z2 = (double *) Malloc(NODE_POINTS_MAX * DOUBLE_SIZE);
    current2 = Z2;

    FiltTbl = (int8_t *) Malloc(NODE_POINTS_MAX * INT8_SIZE);

    X_b = Malloc(XYZ_SIZE);
    Y_b = X_b + INT32_SIZE;
    Z_b = X_b + 2 * INT32_SIZE;

    for (ix = 0; ix < NODE_POINTS_MAX; ++ix) {
	*(FiltTbl + ix) = 0;
    }

    for (i = 0; i < NUM_CELLS; ++i) {
        for (ix = 0; ix < NUM_BINS_X; ++ix) {
            for (iy = 0; iy < NUM_BINS_Y; ++iy) {
                BinTbl[i][ix][iy] = NULL;
                BinU1[i][ix][iy] = 0;
                BinU2[i][ix][iy] = 0;
                BinCnt[i][ix][iy] = 0;
            }
        }
        CellCnt[i] = 0;
    }

    if (NodeID == 0) DistributeSend();
    else DistributeReceive();

    for (i = 0; i < NUM_CELLS; ++i) {
	TriVertex[i] = malloc((2 * CellCnt[i] + 1) * sizeof (LidarPointNode_t **));
	if (TriVertex[i] == NULL) perror("TriVertex[i]");
	for (nt = 0; nt < 2 * CellCnt[i] + 1; ++nt) {
	    TriVertex[i][nt] = malloc(3 * sizeof (LidarPointNode_t *));
	    if (TriVertex[i][nt] == NULL) perror("TriVertex[cell][nt] = malloc()");
	}
	TriEdge[i] = malloc((2 * CellCnt[i] + 1) * sizeof (INT *));
	if (TriEdge[i] == NULL) perror("TriEdge[i]");
	for(nt = 0; nt < 2 * CellCnt[i] + 1; ++nt) {
	    TriEdge[i][nt] = malloc(3 * sizeof (INT));
	    if (TriEdge[i][nt] == NULL) perror("TriEdge[cell][nt] = malloc()");
	}
	estack[i] = malloc(CellCnt[i] * sizeof (INT));
	if (estack[i] == NULL) perror("estack[i] = malloc()");
    }

    WorkerIDs[0] = 0;
    for (i = 1; i <= NUM_WORKERS; ++i) {
        WorkerIDs[i] = i;
        if (pthread_create(&Workers[i], NULL, &ProcessData, &WorkerIDs[i])) perror("pthread_create()");
    }
    ProcessData(&WorkerIDs[0]);

    for (i = 1; i <= NUM_WORKERS; ++i) {
        pthread_join(Workers[i], NULL);
    }

    if (NodeID == 0) MergeReceive();
    else MergeSend();

    gettimeofday(&t_end, NULL);
    t_diff = 1000000 * (t_end.tv_sec - t_start.tv_sec) + t_end.tv_usec - t_start.tv_usec;
    t_diff /= 1000000;
    printf("\nTime taken: %lg seconds\n\n", t_diff);
    fflush(stdout);

    free(PntTbl);
    free(Z2);
    free(FiltTbl);
    free(X_b);

    return 0;
}

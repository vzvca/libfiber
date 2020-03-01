#include <stdio.h>
#include <pcap.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <linux/can.h>
#include <errno.h>
#include <assert.h>

// global variables
char errbuf[PCAP_ERRBUF_SIZE];
const char*    filter = NULL;
const char*    pcapfile = "capture-vcan0.pcap";
const char*    candev = "vcan0";
uint8_t        verbose = 0;
uint8_t        loop = 0;

// c'est le bon offset pour les captures tcpdump de linux
uint32_t       offset = 16;

int canfd = -1;
  
struct bpf_program pcapfilter;

/* --------------------------------------------------------------------------
 *   Data structure used
 * --------------------------------------------------------------------------*/
struct todo {
  pcap_t *fp;
  struct pcap_pkthdr *header;
  const u_char *pkt_data;
};

/* --------------------------------------------------------------------------
 *   Print an help message and leave
 * --------------------------------------------------------------------------*/
void usage( const char *progname, const char *fmt, ...)
{
  if ( fmt != NULL ) {
    char tmp[1024];
    va_list va;
    va_start(va, fmt);
    vsprintf(tmp, fmt, va);
    va_end(va);
    fprintf( stderr, "%s error: %s\n", progname, tmp);
  }
  fprintf( stderr, "Usage:\n");;
  fprintf( stderr, "%s [-v] [-a mcastaddr] [-p mcastport] [-c pcapfile]\n", progname);
  fprintf( stderr, "\t -v           : verbose\n");
  fprintf( stderr, "\t -d can-dev   : can device (default %s)\n", candev);;
  fprintf( stderr, "\t -c file      : pcap file (default %s)\n", pcapfile);
  fprintf( stderr, "\t -f filter    : pcap filter (default none)\n");
  fprintf( stderr, "\t -l num       : how many times to play the file, 0 means infinite looping (default %d)\n", (loop+1));
  fprintf( stderr, "\t -o offset    : offset of payload in pcap packet (default %d is good for linux tcpdump captures)\n", offset);

  exit( (fmt != NULL) ? 1 : 0);
}
/* --------------------------------------------------------------------------
 *   Parse command line switches
 * --------------------------------------------------------------------------*/
void parse( int argc, char **argv)
{
  // decode parameters
  int c = 0;
  while ((c = getopt (argc, argv, "hvc:d:f:l:o:")) != -1)
    {
      switch (c)
	{
	case 'c':    pcapfile = optarg; break;
	case 'd':    candev = optarg; break;
	case 'f':    filter = optarg; break;
	case 'v':    verbose = 1; break;
	case 'l':    loop = atoi(optarg)-1; break;
	case 'o':    offset = atoi(optarg); break;
	case 'h':    usage(argv[0], NULL); break;
	default:     usage(argv[0], "syntax error");
	}
    }
  if ( optind < argc )
    {
      usage(argv[0], "unable to process command line.");
    }
}

int canopen()
{
  canfd = socket(PF_CAN, SOCK_RAW, CAN_RAW);

  if (canfd == -1) {
    perror("socket");
    fprintf( stderr, "Failed to open CAN device '%s'. Leaving...\n", candev);
    exit(1);
  }
  else {
    int res;
    struct sockaddr_can addr;
    struct ifreq ifr;

    strncpy(ifr.ifr_name, candev, sizeof(ifr.ifr_name));
    if (ioctl( canfd, SIOCGIFINDEX, &ifr) != 0) {
      perror("ioctl");
      fprintf( stderr,"Failed to open CAN device '%s'. Leaving...\n", candev);
      exit(1);
    }
    else {
      addr.can_family = AF_CAN;
      addr.can_ifindex = ifr.ifr_ifindex;
      
      res = bind(canfd, (struct sockaddr *) &addr, sizeof(addr));
      if (res == -1) {
	perror("bind");
	fprintf( stderr,"Failed to open CAN device '%s'. Leaving...\n", candev);
	exit(1);
      }

      // filtering
      //can_filter rfilter[2];
      //rfilter[0].can_id = ... some value ...;
      //rfilter[1].can_id = ... some value ...;
      //rfilter[0].can_mask = ... some value ...;
      //rfilter[1].can_mask = ... some value ...;
      
      //setsockopt(m_ComponentImplem->getCanHandle(), SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter));

      fprintf( stderr, "Opened CAN socket %d\n", canfd);
    }
  }
}
  
void canclose()
{
  if (canfd != -1) {
    int res = close(canfd);
    if (res == -1) {
      fprintf( stderr, "Error while closing CAN socket.\n");
    }
  }
}

/* --------------------------------------------------------------------------
 *   Get next packet - reopen file if needed
 * --------------------------------------------------------------------------*/
void next_packet( pcap_t **pfp, struct pcap_pkthdr **phdr, const u_char ** pdata)
{
  int res;
  res = pcap_next_ex(*pfp, phdr, pdata);
  if (res <= 0 || (*phdr)->caplen == 0) {
    // compute remaining loop number
    // -1 means infinite loop
    if ( loop > 0 ) loop--;
    // if looping over file content and eof reached reopen
    if ( (loop != 0) && (res == -2))  {
      // close capture
      if ( filter ) {
	pcap_freecode( &pcapfilter);
      }
      pcap_close(*pfp);
      // open capture
      if ((*pfp = pcap_open_offline(pcapfile, errbuf)) == NULL) {
	fprintf(stderr,"\nUnable to open the file '%s'.\n", pcapfile);
	exit(1);
      }
      if ( verbose ) {
	fprintf( stdout, "Capture file reopened.\n");
      }
      /* compile pcap filter if given */
      if ( filter != NULL && strlen(filter) > 0 ) {
	if ( !pcap_compile(*pfp, &pcapfilter, filter, 0, 0 /* PCAP_NETMASK_UNKNOWN */) ) {
	  fprintf(stderr,"\nUnable to compile filter '%s'.\n", filter);
	  exit(1);
	}
	if ( !pcap_setfilter(*pfp, &pcapfilter)) {
	  fprintf(stderr,"\nUnable to install filter '%s'.\n", filter);
	  exit(1);
	}
      }
    }
    else {
      // it was an error
      printf("Error reading the packets: %s\n", pcap_geterr(*pfp));
      exit(1);
    }
  }
}
/* --------------------------------------------------------------------------
 *   Single processing step
 * --------------------------------------------------------------------------*/
int step( void *clientData )
{
  static int       npkts = 0;      // tracks number of played packets
  static int64_t   usecs = 0;      // remember last number of usec to wait

  struct todo *task= (struct todo*) clientData;
  struct pcap_pkthdr *header = task->header;
  const  u_char *pkt_data = task->pkt_data;
  struct timeval ts, next_send_time, before_send, after_send;
  struct can_frame frame;
  int nbytes;
  
  if ( header == NULL ) {
    do {
      next_packet( &task->fp, &header, &pkt_data);
    }
    while( header->caplen == 0);
  }

  // transfert du paquet
  memset( &frame, 0, sizeof(frame));
  memcpy( &frame, pkt_data + offset, header->caplen - offset);
  frame.can_id |= CAN_EFF_FLAG;
  
  // increase number of processed packets
  // and print i
  npkts = npkts + 1;
  if ( verbose ) {
    if ( (npkts % 100) == 0 ) {
      fprintf( stdout, "Processing packets # %d\n", npkts);
    }
  }

  // remember timestamp of packet to send
  ts = header->ts;
  gettimeofday(&before_send, NULL);
  
  // Send the packet
  if ( npkts % 2 == 0 ) {
    nbytes = write( canfd, &frame, sizeof(frame) );
    if ( nbytes < 0 ) {
      perror("write");
      exit(1);
    }
  }
  
  // schedule next call
  // get next packet from capture
  // @TODO : les paquets sont en double
  do {
    next_packet( &task->fp, &task->header, &task->pkt_data);
  }
  while( task->header->caplen == 0);
  
  // compute usec delta to apply
  gettimeofday(&after_send, NULL);
  after_send.tv_usec -= before_send.tv_usec;
  after_send.tv_sec  -= before_send.tv_sec;
  if ( after_send.tv_usec < 0 ) {
    after_send.tv_sec --;
    after_send.tv_usec += 1000000;
  }

  // Figure out the time at which the next packet should be sent, based
  // on the duration of the payload that we just read:
  next_send_time.tv_usec = task->header->ts.tv_usec - ts.tv_usec - after_send.tv_usec;
  next_send_time.tv_sec  = task->header->ts.tv_sec - ts.tv_sec - after_send.tv_sec;
  if ( next_send_time.tv_usec < 0 ) {
    next_send_time.tv_sec --;
    next_send_time.tv_usec += 1000000;
  }
  
  // if next_send_time.tv_sec < 0 and looping over file
  // reuse last usecs. Note that we test the opposite
  usecs = next_send_time.tv_sec*1000000 + next_send_time.tv_usec;
  if ( usecs < 0 ) {
    if ( verbose ) {
      fprintf( stderr, "being late (%d)\n", -usecs );
    }
    usecs = 0;
  }

  // Delay this amount of time:
  if ( usecs ) {
    usleep(usecs);
  }

  return 1;
}

/* --------------------------------------------------------------------------
 *   boucle
 * --------------------------------------------------------------------------*/
int doloop( struct todo *t )
{
  while(step(t));
  return 0;
}

/* --------------------------------------------------------------------------
 *  Main program
 * --------------------------------------------------------------------------*/
int main(int argc, char **argv)
{
  pcap_t *fp;
  struct todo task;
  
  /* init */
  parse(argc, argv);

  /* Open the capture file */
  if ((fp = pcap_open_offline(pcapfile, errbuf)) == NULL) {
    fprintf(stderr,"\nUnable to open the file '%s'.\n", pcapfile);
    return -1;
  }
  /* compile pcap filter if given */
  if ( filter != NULL && strlen(filter) > 0 ) {
    if ( !pcap_compile(fp, &pcapfilter, filter, 0, 0 /* PCAP_NETMASK_UNKNOWN */) ) {
      fprintf(stderr,"\nUnable to compile filter '%s'.\n", filter);
      return -1;
    }
    if ( !pcap_setfilter(fp, &pcapfilter)) {
      fprintf(stderr,"\nUnable to install filter '%s'.\n", filter);
      return -1;
    }
  }

  /* fill task data */
  task.fp  = fp;
  task.header = NULL;
  task.pkt_data = NULL;

  /* open can */
  canopen();

  // loop
  doloop( &task );

  // close CAN
  canclose();
  
  /* cleanup and exit */
  if ( filter ) {
    pcap_freecode( &pcapfilter);
  }
  pcap_close(fp);
  return 0;
}

/**********************************************************************
Copyright (c) 1991 MPEG/audio software simulation group, All Rights Reserved
musicout.c
**********************************************************************/
/**********************************************************************
 * MPEG/audio coding/decoding software, work in progress              *
 *   NOT for public distribution until verified and approved by the   *
 *   MPEG/audio committee.  For further information, please contact   *
 *   Davis Pan, 508-493-2241, e-mail: pan@3d.enet.dec.com             *
 *                                                                    *
 * VERSION 3.9                                                        *
 *   changes made since last update:                                  *
 *   date   programmers                comment                        *
 * 2/25/91  Douglas Wong        start of version 1.0 records          *
 * 3/06/91  Douglas Wong        rename setup.h to dedef.h             *
 *                              removed extraneous variables          *
 *                              removed window_samples (now part of   *
 *                              filter_samples)                       *
 * 3/07/91  Davis Pan           changed output file to "codmusic"     *
 * 5/10/91  Vish (PRISM)        Ported to Macintosh and Unix.         *
 *                              Incorporated new "out_fifo()" which   *
 *                              writes out last incomplete buffer.    *
 *                              Incorporated all AIFF routines which  *
 *                              are also compatible with SUN.         *
 *                              Incorporated user interface for       *
 *                              specifying sound file names.          *
 *                              Also incorporated user interface for  *
 *                              writing AIFF compatible sound files.  *
 * 27jun91  dpwe (Aware)        Added musicout and &sample_frames as  *
 *                              args to out_fifo (were glob refs).    *
 *                              Used new 'frame_params' struct.       *
 *                              Clean,simplify, track clipped output  *
 *                              and total bits/frame received.        *
 * 7/10/91  Earle Jennings      changed to floats to FLOAT            *
 *10/ 1/91  S.I. Sudharsanan,   Ported to IBM AIX platform.           *
 *          Don H. Lee,                                               *
 *          Peter W. Farrett                                          *
 *10/ 3/91  Don H. Lee          implemented CRC-16 error protection   *
 *                              newly introduced functions are        *
 *                              buffer_CRC and recover_CRC_error      *
 *                              Additions and revisions are marked    *
 *                              with "dhl" for clarity                *
 * 2/11/92  W. Joseph Carter    Ported new code to Macintosh.  Most   *
 *                              important fixes involved changing     *
 *                              16-bit ints to long or unsigned in    *
 *                              bit alloc routines for quant of 65535 *
 *                              and passing proper function args.     *
 *                              Removed "Other Joint Stereo" option   *
 *                              and made bitrate be total channel     *
 *                              bitrate, irrespective of the mode.    *
 *                              Fixed many small bugs & reorganized.  *
 *19 aug 92 Soren H. Nielsen    Changed MS-DOS file name extensions.  *
 * 8/27/93 Seymour Shlien,      Fixes in Unix and MSDOS ports,        *
 *         Daniel Lauzon, and                                         *
 *         Bill Truerniet                                             *
 **********************************************************************/

#include        "common.h"
#include        "decoder.h"

/********************************************************************
/*
/*        This part contains the MPEG I decoder for Layers I & II.
/*
/*********************************************************************/

/****************************************************************
/*
/*        For MS-DOS user (Turbo c) change all instance of malloc
/*        to _farmalloc and free to _farfree. Compiler model hugh
/*        Also make sure all the pointer specified are changed to far.
/*
/*****************************************************************/

/*********************************************************************
/*
/* Core of the Layer II decoder.  Default layer is Layer II.
/*
/*********************************************************************/

/* Global variable definitions for "musicout.c" */

char *programName;

/* Implementations */

main(argc, argv)
int argc;
char **argv;
{
typedef short PCM[2][3][SBLIMIT];
    PCM FAR *pcm_sample;
typedef unsigned int SAM[2][3][SBLIMIT];
    SAM FAR *sample;
typedef double FRA[2][3][SBLIMIT];
    FRA FAR *fraction;
typedef double VE[2][HAN_SIZE];
    VE FAR *w;

    Bit_stream_struc  bs;
    frame_params      fr_ps;
    layer             info;
    FILE              *musicout;
    unsigned long     sample_frames;

    int               i, j, k, stereo, done=FALSE, clip, sync;
    int               error_protection, crc_error_count, total_error_count;
    int               x;
    char              default_file_name[MAX_NAME_SIZE]; 
    unsigned int      old_crc, new_crc;
    unsigned int      bit_alloc[2][SBLIMIT], scfsi[2][SBLIMIT],
                      scale_index[2][3][SBLIMIT];
    unsigned long     bitsPerSlot, samplesPerFrame, frameNum = 0;
    unsigned long     frameBits, gotBits = 0;
    IFF_AIFF          pcm_aiff_data;
    char              encoded_file_name[MAX_NAME_SIZE];
    char              decoded_file_name[MAX_NAME_SIZE];
    char              t[50];
    int               need_aiff;
    int topSb = 0;

#ifdef  MACINTOSH
    console_options.nrows = MAC_WINDOW_SIZE;
    argc = ccommand(&argv);
#endif

    /* Most large variables are declared dynamically to ensure
       compatibility with smaller machines */

    pcm_sample = (PCM FAR *) mem_alloc((long) sizeof(PCM), "PCM Samp");
    sample = (SAM FAR *) mem_alloc((long) sizeof(SAM), "Sample");
    fraction = (FRA FAR *) mem_alloc((long) sizeof(FRA), "fraction");
    w = (VE FAR *) mem_alloc((long) sizeof(VE), "w");

    fr_ps.header = &info;
    fr_ps.tab_num = -1;                /* no table loaded */
    fr_ps.alloc = NULL;
    for (i=0;i<HAN_SIZE;i++) for (j=0;j<2;j++) (*w)[j][i] = 0.0;

    programName = argv[0];
    if(argc==1) {        /* no command line args -> interact */
       do {
          printf ("Enter encoded file name <required>: ");
          gets (encoded_file_name);
          if (encoded_file_name[0] == NULL_CHAR)
             printf ("Encoded file name is required. \n");
       } while (encoded_file_name[0] == NULL_CHAR);
       printf (">>> Encoded file name is: %s \n", encoded_file_name);
       
    x=0;
    while (x <= MAX_NAME_SIZE)
    {
        default_file_name[x] = NULL_CHAR;
        ++x;
        }
    x=0;
    while (x <= 8)
    {
   default_file_name[x] = encoded_file_name[x];
    if (encoded_file_name[++x] == '.') 
        x = 9;
  
    }
    
 
    strcat(default_file_name,DFLT_OPEXT);

    printf("Enter MPEG decoded output file name <%s>: ",
           default_file_name); /* 92-08-19 shn */
    gets(decoded_file_name);
    if (decoded_file_name[0] == NULL_CHAR) {
 
        /* replace old extension with new one, 92-08-19 shn */
       strcpy(decoded_file_name,default_file_name);
    }
       
      
   
       printf (">>> MPEG decoded file name is: %s \n", decoded_file_name);

       printf(
          "Do you wish to write an AIFF compatible sound file ? (y/<n>) : ");
       gets(t);
       if (*t == 'y' || *t == 'Y') need_aiff = TRUE;
       else                        need_aiff = FALSE;
       if (need_aiff)
            printf(">>> An AIFF compatible sound file will be written\n");
       else printf(">>> A non-headered PCM sound file will be written\n");

       printf(
          "Do you wish to exit (last chance before decoding) ? (y/<n>) : ");
       gets(t);
       if (*t == 'y' || *t == 'Y') exit(0);
    }
    else {        /* interpret CL Args */
       int i=0, err=0;

       need_aiff = FALSE;
       encoded_file_name[0] = '\0';
       decoded_file_name[0] = '\0';

       while(++i<argc && err == 0) {
          char c, *token, *arg, *nextArg;
          int  argUsed;

          token = argv[i];
          if(*token++ == '-') {
             if(i+1 < argc) nextArg = argv[i+1];
             else           nextArg = "";
             argUsed = 0;
             while(c = *token++) {
                if(*token /* NumericQ(token) */) arg = token;
                else                             arg = nextArg;
                switch(c) {
                   case 's':  topSb = atoi(arg); argUsed = 1;
                      if(topSb<1 || topSb>SBLIMIT) {
                         fprintf(stderr, "%s: -s band %s not %d..%d\n",
                                 programName, arg, 1, SBLIMIT);
                         err = 1;
                      }
                      break;
                   case 'A':  need_aiff = TRUE; break;
                   default:   fprintf(stderr,"%s: unrecognized option %c\n",
                                      programName, c);
                      err = 1; break;
                }
                if(argUsed) {
                   if(arg == token) token = ""; /* no more from token */
                   else             ++i; /* skip arg we used */
                   arg = ""; argUsed = 0;
                }
             }
          }
          else {
             if(encoded_file_name[0] == '\0')
                strcpy(encoded_file_name, argv[i]);
             else
                if(decoded_file_name[0] == '\0')
                   strcpy(decoded_file_name, argv[i]);
                else {
                   fprintf(stderr,
                           "%s: excess arg %s\n", programName, argv[i]);
                   err = 1;
                }
          }
       }

       if(err || encoded_file_name[0] == '\0') usage();  /* never returns */

       if(decoded_file_name[0] == '\0') {
          strcpy(decoded_file_name, encoded_file_name);
          strcat(decoded_file_name, DFLT_OPEXT);
       }

    }
    /* report results of dialog / command line */
    printf("Input file = '%s'  output file = '%s'\n",
           encoded_file_name, decoded_file_name);
    if(need_aiff) printf("Output file written in AIFF format\n");

    if(strcmp(decoded_file_name,"stdout") == 0) musicout = stdout;
    else {
         if ((musicout = fopen(decoded_file_name, "w+b")) == NULL) {
            printf ("Could not create \"%s\".\n", decoded_file_name);
            exit(1);
         }
    }

    open_bit_stream_r(&bs, encoded_file_name, BUFFER_SIZE);

    if (need_aiff)
       if (aiff_seek_to_sound_data(musicout) == -1) {
          printf("Could not seek to PCM sound data in \"%s\".\n",
                 decoded_file_name);
          exit(1);
       }

    sample_frames = 0;

    while (!end_bs(&bs)) {

       sync = seek_sync(&bs, SYNC_WORD, SYNC_WORD_LNGTH);
       frameBits = sstell(&bs) - gotBits;
       if(frameNum > 0)        /* don't want to print on 1st loop; no lay */
          if(frameBits%bitsPerSlot)
             fprintf(stderr,"Got %ld bits = %ld slots plus %ld\n",
                     frameBits, frameBits/bitsPerSlot, frameBits%bitsPerSlot);
       gotBits += frameBits;

       if (!sync) {
          printf("Frame cannot be located\n");
          printf("Input stream may be empty\n");
          done = TRUE;
          /* finally write out the buffer */
          if (info.lay == 2) out_fifo(*pcm_sample, 3, &fr_ps, done,
                                      musicout, &sample_frames);
          else               out_fifo(*pcm_sample, 1, &fr_ps, done,
                                      musicout, &sample_frames);
          break;
       }

       decode_info(&bs, &fr_ps);
       hdr_to_frps(&fr_ps);
       stereo = fr_ps.stereo;
       error_protection = info.error_protection;
       crc_error_count = 0;
       total_error_count = 0;
       if(frameNum == 0) WriteHdr(&fr_ps, stdout);  /* printout layer/mode */

       fprintf(stderr, "{%4lu}\r", frameNum++); fflush(stderr);
       if (error_protection) buffer_CRC(&bs, &old_crc);

       switch (info.lay) {

          case 1: {
             bitsPerSlot = 32;        samplesPerFrame = 384;
             I_decode_bitalloc(&bs,bit_alloc,&fr_ps);
             I_decode_scale(&bs, bit_alloc, scale_index, &fr_ps);

             if (error_protection) {
                I_CRC_calc(&fr_ps, bit_alloc, &new_crc);
                if (new_crc != old_crc) {
                   crc_error_count++;
                   total_error_count++;
                   recover_CRC_error(*pcm_sample, crc_error_count,
                                     &fr_ps, musicout, &sample_frames);
                   break;
                }
                else crc_error_count = 0;
             }

             clip = 0;
             for (i=0;i<SCALE_BLOCK;i++) {
                I_buffer_sample(&bs,(*sample),bit_alloc,&fr_ps);
                I_dequantize_sample(*sample,*fraction,bit_alloc,&fr_ps);
                I_denormalize_sample((*fraction),scale_index,&fr_ps);
                if(topSb>0)        /* clear channels to 0 */
                   for(j=topSb; j<fr_ps.sblimit; ++j)
                      for(k=0; k<stereo; ++k)
                         (*fraction)[k][0][j] = 0;

                for (j=0;j<stereo;j++) {
                   clip += SubBandSynthesis (&((*fraction)[j][0][0]), j,
                                             &((*pcm_sample)[j][0][0]));
                }
                out_fifo(*pcm_sample, 1, &fr_ps, done,
                         musicout, &sample_frames);
             }
             if(clip > 0) printf("%d output samples clipped\n", clip);
             break;
          }

          case 2: {
             bitsPerSlot = 8;        samplesPerFrame = 1152;
             II_decode_bitalloc(&bs, bit_alloc, &fr_ps);
             II_decode_scale(&bs, scfsi, bit_alloc, scale_index, &fr_ps);

             if (error_protection) { 
                II_CRC_calc(&fr_ps, bit_alloc, scfsi, &new_crc);
                if (new_crc != old_crc) {
                   crc_error_count++;
                   total_error_count++;
                   recover_CRC_error(*pcm_sample, crc_error_count,
                                     &fr_ps, musicout, &sample_frames);
                   break;
                }
                else crc_error_count = 0;
             }

             clip = 0;
             for (i=0;i<SCALE_BLOCK;i++) {
                II_buffer_sample(&bs,(*sample),bit_alloc,&fr_ps);
                II_dequantize_sample((*sample),bit_alloc,(*fraction),&fr_ps);
                II_denormalize_sample((*fraction),scale_index,&fr_ps,i>>2);

                if(topSb>0)        /* debug : clear channels to 0 */
                   for(j=topSb; j<fr_ps.sblimit; ++j)
                      for(k=0; k<stereo; ++k)
                         (*fraction)[k][0][j] =
                         (*fraction)[k][1][j] =
                         (*fraction)[k][2][j] = 0;

                for (j=0;j<3;j++) for (k=0;k<stereo;k++) {
                   clip += SubBandSynthesis (&((*fraction)[k][j][0]), k,
                                             &((*pcm_sample)[k][j][0]));
                }
                out_fifo(*pcm_sample, 3, &fr_ps, done, musicout,
                         &sample_frames);
             }
             if(clip > 0) printf("%d samples clipped\n", clip);
             break;
          }

       }

    }

    if (need_aiff) {
       pcm_aiff_data.numChannels       = stereo;
       pcm_aiff_data.numSampleFrames   = sample_frames;
       pcm_aiff_data.sampleSize        = 16;
       pcm_aiff_data.sampleRate        = s_freq[info.sampling_frequency]*1000;
#ifdef IFF_LONG       
       pcm_aiff_data.sampleType        = IFF_ID_SSND;
#else
       strncpy(&pcm_aiff_data.sampleType,IFF_ID_SSND,4);
#endif
       pcm_aiff_data.blkAlgn.offset    = 0;
       pcm_aiff_data.blkAlgn.blockSize = 0;
 
       if (aiff_write_headers(musicout, &pcm_aiff_data) == -1) {
          printf("Could not write AIFF headers to \"%s\"\n",
                 decoded_file_name);
          exit(2);
       }
    }

    printf("Avg slots/frame = %.3f; b/smp = %.2f; br = %.3f kbps\n",
           (FLOAT) gotBits / (frameNum * bitsPerSlot),
           (FLOAT) gotBits / (frameNum * samplesPerFrame),
           (FLOAT) gotBits / (frameNum * samplesPerFrame) *
           s_freq[info.sampling_frequency]);

    close_bit_stream_r(&bs);
    fclose(musicout);

    /* for the correct AIFF header information */
    /*             on the Macintosh            */
    /* the file type and the file creator for  */
    /* Macintosh compatible Digidesign is set  */

#ifdef  MACINTOSH
    if (need_aiff) set_mac_file_attr(decoded_file_name, VOL_REF_NUM,
                                     CREATR_DEC_AIFF, FILTYP_DEC_AIFF);
    else           set_mac_file_attr(decoded_file_name, VOL_REF_NUM,
                                     CREATR_DEC_BNRY, FILTYP_DEC_BNRY);
#endif

    printf("Decoding of \"%s\" is finished\n", encoded_file_name);
    printf("The decoded PCM output file name is \"%s\"\n", decoded_file_name);
    if (need_aiff)
       printf("\"%s\" has been written with AIFF header information\n",
              decoded_file_name);

    exit( 0 );
}

static void usage()  /* print syntax & exit */
{
   fprintf(stderr,
      "usage: %s                         queries for all arguments, or\n",
       programName);
   fprintf(stderr,
      "       %s [-A][-s sb] inputBS [outPCM]\n", programName);
   fprintf(stderr,"where\n");
   fprintf(stderr," -A       write an AIFF output PCM sound file\n");
   fprintf(stderr," -s sb    resynth only up to this sb (debugging only)\n");
   fprintf(stderr," inputBS  input bit stream of encoded audio\n");
   fprintf(stderr," outPCM   output PCM sound file (dflt inName+%s)\n",
           DFLT_OPEXT);
   exit(1);
}

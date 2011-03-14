#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
   sumfil: sum filterbank format power spectra time series.   
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "header.h"
#include "sigproc.h"
#include <string.h>
#include <gsl/gsl_histogram.h>

int wapp_header_size, wapp_incfile_length;
int nbins;
	double period;

unsigned char quantize(double d, double mean, double sigma)
{
	return (unsigned char) (    (    (d - (mean - (3 * sigma))) / (6 * sigma)   ) * 255.0   );
}

main(int argc, char *argv[]) 
{
	FILE *fileptr[50];
	FILE *output;
    char buffer[8192];
    double spectra_sum[128];
	char filename[80],*telescope,*backend,*datatype,message[80],unit[16], outfile[120];
	int i,j, k, year,month,day,check,rah,ram,ded,dem, nfiles, opened=0;
	double ras,des,frac,tobs;
	char sra[6],sde[6],decsign;
	int raw,uth,utm,uts;
	long long numsamps,datasize,headersize;
    long long mindatasize=0;
	int writeobsdbline;

	/* get these from first file on command line, apply to output file */
	int output_nchans=0, output_nbits=0, output_nifs=0;
	long long output_datasize=0;
    long long output_headersize=0;
    int output_ptr;
	char quantval;
    
    double mean=0.0, sigma=0.0;
    
    
    /* Create histograms and set ranges   */
	/* _full for all pulse detections     */
	/* _clean for rfi rejected detections */
	gsl_histogram *spectra_quant = gsl_histogram_alloc (11221);
	gsl_histogram_set_ranges_uniform (spectra_quant, 0, 11220);

    
  /* work out how many files are on the command line */
  i=1;
  nfiles=0;
  while(file_exists(argv[i]) && (i < argc)) {
        //printf("opening %s\n", argv[i]);
  	    nfiles++;
        i++;
  }
  if (!nfiles) {
	  error_message("no input files supplied on command line!");
	  exit(1);
  }

  if (nfiles > 50) {
      error_message("too many input files supplied on command line (max 50)!");
	  exit(1);
  }


  for(i = 0; i<nfiles; i++) {
			  fileptr[i]=open_file(argv[i+1],"rb");
  }


  if (argc>nfiles) {
	 i=nfiles+1;
	 while (i<argc) {
		if (strings_equal(argv[i],"-o")) {
		
		  /* get and open file for output */
		  strcpy(outfile,argv[++i]);
		  if(file_exists(outfile)) {
			  sprintf(message,"output file (%s) exists!",argv[i]);
			  error_message(message);
			  exit(1);
		  }
		  output=fopen(outfile,"wb");
		  opened=1;
		} else {
			/* unknown argument passed down - stop! */
			sprintf(message,"unknown argument (%s) passed to filterbank.",argv[i]);
			error_message(message);
			exit(1);
		}
		i++;
	  }
  }

  if (!opened) {
	  error_message("must have an output file (-o <output>)!");
	  exit(1);
  }


	pulsarcentric=barycentric=0;
	writeobsdbline=0;
	


    
  for(i = 0; i<nfiles; i++) {
		headersize=read_header(fileptr[i]);		
	    rewind(fileptr[i]);
		datasize=sizeof_file(argv[i+1])-headersize;
	    numsamps=nsamples(argv[i+1],headersize,nbits,nifs,nchans);
		if(output_datasize == 0 || datasize < output_datasize) {
			output_datasize = datasize;
		    output_headersize = headersize;
			output_ptr = i;
		}
		if(output_nchans == 0) {
		  output_nchans = nchans; 
		  output_nbits = nbits; 
		  output_nifs = nifs;		
		}
        if(nchans != output_nchans || nbits != output_nbits || nifs != output_nifs) {
			sprintf(message,"channel/if/bit mismatch, exiting...");
			error_message(message);
			exit(1);
        }
        
        //printf("file: %s headersize: %d nbits: %d nifs: %d nchans: %d\n", argv[i+1], headersize, nbits, nifs, nchans); 
  }


  if (output_nbits != 8) {
	  error_message("This routine only works on 8bit data!");
	  exit(1);
  }
  
 
  printf("minimum data size is: %d\n", output_datasize);
  
  printf("will dump: %d\n", (output_datasize / (long long) (output_nifs * output_nchans)));
  
  headersize=read_header(fileptr[output_ptr]);
  rewind(fileptr[output_ptr]);

  printf("header size lead file: %d\n", headersize); 
  fread(buffer, sizeof(char), headersize, fileptr[output_ptr]);
  fwrite(buffer, sizeof(char), headersize, output);
  rewind(fileptr[output_ptr]);

 /* bump past header for all input files */
   for(i = 0; i<nfiles; i++) {
		headersize=read_header(fileptr[i]);	
	}
  

/* outer loop, read through 8000 spectra (1024000 spectral points), histogram, calculate sigma for quantizing to 8 bits */

  for (j = 0; j < 8000; j++){
  
	 /* read n spectra (1 spectra x n files), sum */
	 for(i=0;i<128;i++) spectra_sum[i] = 0.0;
	 for(i = 0; i<nfiles; i++) {
		  fread(buffer, sizeof(char), (output_nifs * output_nchans), fileptr[i]);
          for(k=0; k<128; k++) spectra_sum[k] = (spectra_sum[k] + ((double) buffer[k]));
	  }
	  //for(i=0; i<128; i++) printf("%f\n", spectra_sum[i]);
	  for(i=0; i<128; i++) gsl_histogram_increment(spectra_quant, spectra_sum[i]);
	  //usleep(5000000);	
   }	



//printf("sigma: %f\n", gsl_histogram_sigma(spectra_quant));
//printf("mean: %f\n", gsl_histogram_mean(spectra_quant));

sigma = gsl_histogram_sigma(spectra_quant);
mean = gsl_histogram_mean(spectra_quant);

gsl_histogram_free (spectra_quant);

 /* rewind and bump past header for all input files */
   for(i = 0; i<nfiles; i++) {
		rewind( fileptr[i] );
		headersize=read_header(fileptr[i]);	
	}


/* outer loop, read through all spectra, quantize to 8 bits, write to file */

  for (j = 0; j < (output_datasize / (long long) (output_nifs * output_nchans)); j++){
  
	 /* read n spectra (1 spectra x n files), sum */
	 for(i=0;i<128;i++) spectra_sum[i] = 0.0;
	 for(i = 0; i<nfiles; i++) {
		  fread(buffer, sizeof(char), (output_nifs * output_nchans), fileptr[i]);
          for(k=0; k<128; k++) spectra_sum[k] = (spectra_sum[k] + ((double) buffer[k]));
	  }
	  for(i=0; i<128; i++) {	  
		  quantval = quantize(spectra_sum[i], mean, sigma);
		  fwrite(&quantval, sizeof(char), 1, output);	  
		  //printf("%f : %d\n", spectra_sum[i], quantval);
	  }
		  //usleep(5000000);

   }



/*
	if (argc>1) {
		print_version(argv[0],argv[1]);
		if (help_required(argv[1])) {
			header_help();
			exit(0);
		} else if (file_exists(argv[1])) {
			strcpy(filename,argv[1]);
			fileptr=open_file(filename,"rb");
		} else if (!file_exists(argv[1]) && (strncmp(argv[1],"-",1) !=0)) {
			sprintf(message,"Data file: %s not found...\n",argv[1]);
			error_message(message);
			exit(1);
		}
	}
*/

}

// Scalpel Copyright (C) 2005-6 by Golden G. Richard III.
// Written by Golden G. Richard III.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301, USA.

// Scalpel is a complete rewrite of the foremost 0.69 file carver to
// increase speed and support execution on machines with minimal
// resources (e.g., < 256MB RAM).
//
// Thanks to Kris Kendall, Jesse Kornblum, et al for their work on
// foremost.

#include "scalpel.h"

// Returns TRUE if the directory exists and is empty. 
// If the directory does not exist, an attempt is made to 
// create it.  On error, returns FALSE 
int outputDirectoryOK(char *dir) {

  DIR *temp;
  struct dirent *entry;
  int i;
  mode_t newDirectoryMode;
  
  if ((temp = opendir(dir)) == NULL) {
    // If the directory doesn't exist (ENOENT), we will create it
    if (errno == ENOENT) {
      // The directory mode values come from the chmod(2) man page 
#ifdef  __MINGW32__
      newDirectoryMode = 0;
      if(mkdir(dir)){
#else
	newDirectoryMode = (S_IRUSR | S_IWUSR | S_IXUSR |
			    S_IRGRP | S_IWGRP | S_IXGRP |
			    S_IROTH | S_IWOTH);
	if (mkdir(dir,newDirectoryMode)) {
#endif	
	  
	  fprintf (stderr,"An error occured while trying to create %s - %s\n",
		   dir,strerror(errno));
	  return FALSE;
	}
	
	// try to open directory
	if ((temp = opendir(dir)) == NULL) {
	  fprintf (stderr,"An error occured while trying to open %s - %s\n",
		   dir,strerror(errno));
	  return FALSE;
	}
      }
      else {
	fprintf (stderr,"An error occured while trying to open %s - %s\n",
		 dir,strerror(errno));
	return FALSE;
      }
    }
    
    // verify directory is empty--there should be only two entries,
    // "." and ".." 
    i = 0;
    while((entry = readdir(temp))) {
      if (i>1) {
	fprintf(stderr, NONEMPTYDIR_ERROR_MSG);
	return FALSE;
      }
      i++;
    }
    closedir(temp);
    return TRUE;
  }
  
  // open audit file
  int openAuditFile(struct scalpelState* state){
    time_t now = time(NULL);
    char* timestring = ctime(&now);
    char fn[MAX_STRING_LENGTH];
    
    if (!outputDirectoryOK(state->outputdirectory)) {
      return SCALPEL_ERROR_FILE_OPEN;
    }
    
    snprintf(fn,MAX_STRING_LENGTH,"%s/audit.txt",
	     state->outputdirectory);
    
    if (!(state->auditFile = fopen(fn,"w"))) {    
      fprintf(stderr,"Couldn't open %s -- %s\n",fn,strerror(errno));
      return SCALPEL_ERROR_FILE_OPEN;
    }
    
    fprintf (state->auditFile,
	     "\nScalpel version %s audit file\n"
	     "Started at %sCommand line:\n%s\n\n"
	     "Output directory: %s\n"
	     "Configuration file: %s\n",
	     SCALPEL_VERSION, timestring, state->invocation,
	     state->outputdirectory,state->conffile);
    
    return SCALPEL_OK;
}


int closeFile(FILE* f) {

  time_t now = time(NULL);
  char* timestring = ctime(&now);

  fprintf (f, "\n\nCompleted at %s", timestring);

  if (fclose(f)) {
    return SCALPEL_ERROR_FILE_CLOSE;
  }

  return SCALPEL_OK;
}


// helper function for measureOpenFile(), based on e2fsprogs utility 
// function valid_offset()

 static int valid_offset(int fd, off_t offset) {
     char ch;
     if (lseek(fd, offset, SEEK_SET) < 0) {
       return 0;
     }
     if (read(fd, &ch, 1) < 1) {
       return 0;
     }
     return 1;
 }


// Return the remaining size, in bytes, of an open file stream. On
// error, return -1.  Handling raw device files is substantially more
// complicated than image files.  For Linux, an ioctl() is used.  For
// other operating systems, a "binary search" technique similar to
// that in e2fsprogs getsize.c is used.
unsigned long long measureOpenFile(FILE *f, struct scalpelState* state) {
    (void)state;

    off_t cur = ftello(f);
    if (cur == (off_t)-1) {
        if (state && state->modeVerbose)
            fprintf(stdout, "ftello() failed: %s\n", strerror(errno));
        return (unsigned long long)-1;
    }

    if (fseeko(f, 0, SEEK_END) != 0) {
        if (state && state->modeVerbose)
            fprintf(stdout, "fseeko(SEEK_END) failed: %s\n", strerror(errno));
        return (unsigned long long)-1;
    }
    off_t end = ftello(f);
    if (end == (off_t)-1) {
        if (state && state->modeVerbose)
            fprintf(stdout, "ftello() at end failed: %s\n", strerror(errno));
        fseeko(f, cur, SEEK_SET);
        return (unsigned long long)-1;
    }

    if (fseeko(f, cur, SEEK_SET) != 0) {
        if (state && state->modeVerbose)
            fprintf(stdout, "fseeko(restore) failed: %s\n", strerror(errno));
        return (unsigned long long)-1;
    }

    return (unsigned long long)end;
}




// amrfinder.cpp

/*===========================================================================
*
*                            PUBLIC DOMAIN NOTICE                          
*               National Center for Biotechnology Information
*                                                                          
*  This software/database is a "United States Government Work" under the   
*  terms of the United States Copyright Act.  It was written as part of    
*  the author's official duties as a United States Government employee and 
*  thus cannot be copyrighted.  This software/database is freely available 
*  to the public for use. The National Library of Medicine and the U.S.    
*  Government have not placed any restriction on its use or reproduction.  
*                                                                          
*  Although all reasonable efforts have been taken to ensure the accuracy  
*  and reliability of the software and data, the NLM and the U.S.          
*  Government do not and cannot warrant the performance or results that    
*  may be obtained by using this software or data. The NLM and the U.S.    
*  Government disclaim all warranties, express or implied, including       
*  warranties of performance, merchantability or fitness for any particular
*  purpose.                                                                
*                                                                          
*  Please cite the author in any work or product based on this material.   
*
* ===========================================================================
*
* Author: Vyacheslav Brover
*
* File Description:
*   AMRFinder
*
* Dependencies: NCBI BLAST, HMMer
*               cat, cp, cut, grep, head, mkdir, mv, nproc, sed, sort, tail
*
* Release changes:
*   3.6.4  01/03/2020 PD-3230   sorting of report rows: protein accession is ignored if contig is available
*   3.6.3  01/03/2020 PD-3230   sorting of report rows
*          12/28/2019           QC in dna_mutation
*   3.6.2  12/27/2019 PD-3230   Redundant reported lines are removed for mutated reference proteins
*                               Reports are sorted by sort
*   3.6.1  12/27/2019 PD-3230   Mutated proteins are added to AMRProt
*   3.5.10 12/20/2019           --log
*   3.5.9  12/19/2019 PD-3294   blastx parameters: space added
*   3.5.8  12/18/2019 issues/19 changed message if db path is bad
*   3.5.7  12/18/2019 PD-3289   improved message for gff_check failure
*   3.5.6  12/18/2019 PD-3269   --gpipe is removed, --pgapx is replaced by --pgap
*   3.5.5  12/17/2019 PD-3287   short proteins at an end of a contig are reported
*   3.5.4  12/17/2019 PD-3287   truncated short proteins are not reported
*   3.5.3  12/16/2019 PD-3279   GPipe-GenColl assemblies, --gpipe_org
*                     GP-28025
*   3.5.2  12/13/2019 PD-3269   New flag --pgapx
*   3.5.1  12/12/2019 PD-3277   Files AMRProt-mutation.tab, AMRProt-suppress, AMR_DNA-<TAXGROUP>.tab and taxgroup.tab have headers
*   3.4.3  12/11/2019 PD-2171   --mutation_all bug
*                               --debug does not imply "-verbose 1"
*   3.4.2  12/10/2019 PD-3209   alignment correction for mutations
*                               point_mut.{hpp,cpp} -> alignment.{hpp,cpp}
*                               dna_point_mut.cpp -> dna_mutation.cpp
*                               AMRProt-point_mut.tab -> AMRProt-mutation.tab
*                               protein resistance: "point_mutation" -> "mutation"
*                               amrfinder: --point_mut_all -> --mutation_all
*                     PD-3232   mutation detection redesign
*                     PD-3267   mutation in a mutated context
*   3.4.1  12/03/2019 PD-3193   AMR_DNA-*.tab: column "genesymbol" is removed
*                               product name is fixed for point mutations
*                               point_mut.cpp -> dna_point_mut.cpp
*   3.3.2  11/26/2019 PD-3193   Indel mutations: partially implemented
*                               Bug fixed: protein point mutations were reported incorrectly if there was an offset w.r.t. the reference sequence
*                               Files AMRProt-point_mut.tab and AMR_DNA-<taxgroup>.tab: columns allele, symbol are removed
*                               Files taxgroup.list and gpipe.tab are replaced by taxgroup.tab
*   3.3.1  11/22/2019 PD-3206   New files: taxgroup.list, gpipe.tab; new option --list_organisms
*   3.2.3  11/14/2019 PD-3192   Fixed error made by PD-3190
*   3.2.3  11/13/2019 PD-3190   organisms for --gpipe
*   3.2.3  11/12/2019 PD-3187   Sequence name is always from AMRProt, not from fam.tab
*   3.2.2  11/06/2019 PD-2244   Added "LANG=C" before "sort"
*
*/


#ifdef _MSC_VER
  #error "UNIX is required"
#endif
   
#undef NDEBUG 
#include "common.inc"

#include "common.hpp"
using namespace Common_sp;



// PAR!
// PD-3051
#ifdef SVN_REV
  #define SOFTWARE_VER SVN_REV
#else
  #define SOFTWARE_VER "3.6.4"
#endif
#define DATA_VER_MIN "2019-12-26.1"  



namespace 
{
  
  
// PAR
constexpr size_t threads_max_min = 1;  
constexpr size_t threads_def = 4;
// Cf. amr_report.cpp
constexpr double ident_min_def = 0.9;
constexpr double partial_coverage_min_def = 0.5;
  
    
#define HELP  \
"Identify AMR and virulence genes in proteins and/or contigs and print a report\n" \
"\n" \
"DOCUMENTATION\n" \
"    See https://github.com/ncbi/amr/wiki for full documentation\n" \
"\n" \
"UPDATES\n" \
"    Subscribe to the amrfinder-announce mailing list for database and software update notifications:\n" \
"    https://www.ncbi.nlm.nih.gov/mailman/listinfo/amrfinder-announce"

		

// ThisApplication

struct ThisApplication : ShellApplication
{
  ThisApplication ()
    : ShellApplication (HELP, true, true, true)
    {
    	addFlag ("update", "Update the AMRFinder database", 'u');  // PD-2379
    	addKey ("protein", "Protein FASTA file to search", "", 'p', "PROT_FASTA");
    	addKey ("nucleotide", "Nucleotide FASTA file to search", "", 'n', "NUC_FASTA");
    	addKey ("gff", "GFF file for protein locations. Protein id should be in the attribute 'Name=<id>' (9th field) of the rows with type 'CDS' or 'gene' (3rd field).", "", 'g', "GFF_FILE");
      addFlag ("pgap", "Input files PROT_FASTA, NUC_FASTA and GFF_FILE are created by the NCBI PGAP. Prefixes 'gnl|' or 'lcl|' are removed from the accessions in NUC_FASTA");
    	addKey ("database", "Alternative directory with AMRFinder database. Default: $AMRFINDER_DB", "", 'd', "DATABASE_DIR");
    	addKey ("ident_min", "Minimum identity for nucleotide hit (0..1). -1 means use a curated threshold if it exists and " + toString (ident_min_def) + " otherwise", "-1", 'i', "MIN_IDENT");
    	addKey ("coverage_min", "Minimum coverage of the reference protein (0..1)", toString (partial_coverage_min_def), 'c', "MIN_COV");
      addKey ("organism", "Taxonomy group. To see all possible taxonomy groups use the --list_organisms flag", "", 'O', "ORGANISM");
      addFlag ("list_organisms", "Print the list of all possible taxonomy groups for mutations identification and exit", 'l');
    	addKey ("translation_table", "NCBI genetic code for translated BLAST", "11", 't', "TRANSLATION_TABLE");
    	addFlag ("plus", "Add the plus genes to the report");  // PD-2789
      addFlag ("report_common", "Report proteins common to a taxonomy group");  // PD-2756
    	addKey ("mutation_all", "File to report all target positions of reference mutations", "", '\0', "MUT_ALL_FILE");
    	addKey ("blast_bin", "Directory for BLAST. Deafult: $BLAST_BIN", "", '\0', "BLAST_DIR");
    //addKey ("hmmer_bin" ??
      addKey ("output", "Write output to OUTPUT_FILE instead of STDOUT", "", 'o', "OUTPUT_FILE");
      addFlag ("quiet", "Suppress messages to STDERR", 'q');
    //addFlag ("gpipe", "NCBI internal GPipe processing: protein identifiers in the protein FASTA file have format 'gnl|<project>|<accession>'");
      addFlag ("gpipe_org", "NCBI internal GPipe organism names");
    	addKey ("parm", "amr_report parameters for testing: -nosame -noblast -skip_hmm_check -bed", "", '\0', "PARM");
	    version = SOFTWARE_VER;  
	  #if 0
	    setRequiredGroup ("protein",    "Input");
	    setRequiredGroup ("nucleotide", "Input");
	  #endif
	    // threads_max: do not include blast/hmmsearch's threads ??
    }



  void initEnvironment () final
  {
    ShellApplication::initEnvironment ();
    var_cast (name2arg ["threads"] -> asKey ()) -> defaultValue = to_string (threads_def);  
  }
  
  
  
  bool blastThreadable (const string &blast,
                        const string &logFName) const
  {
    try { exec (fullProg (blast) + " -help | grep '^ *\\-num_threads' > " + logFName + " 2> " + logFName, logFName); }
      catch (const runtime_error &) 
        { return false; }
    return true;        
  }



  size_t get_threads_max_max () const
  {
  #if __APPLE__
    int count;
    size_t count_len = sizeof(count);
    sysctlbyname("hw.logicalcpu", &count, &count_len, NULL, 0);
    // fprintf(stderr,"you have %i cpu cores", count);
    return count;
  #else
    exec ("nproc --all > " + tmp + ".nproc");
    LineInput f (tmp + ".nproc");
    return str2<size_t> (f. getString ());
  #endif
  }



  string file2link (const string &fName) const
  {
  #if 1
    const string s (realpath (fName. c_str(), nullptr));
    if (s == fName)
      return string ();
    return s;
  #else
    exec ("file " + fName + " > " + tmp + ".file");
    
    LineInput f (tmp + ".file");
    string s (f. getString ());
    
    trimPrefix (s, fName + ": ");
    if (isLeft (s, "symbolic link to "))
      return s;
    return string ();
  #endif
  }



  StringVector db2organisms (const string &db) const
  {
    exec ("tail -n +2 " + db + "/AMRProt-mutation.tab | cut -f 1 > " + tmp + ".prot_org");
    exec ("tail -n +2 " + db + "/taxgroup.tab         | cut -f 1 > " + tmp + ".tax_org");
    exec ("cat " + tmp + ".prot_org " + tmp + ".tax_org | sort -u > " + tmp + ".org");
    LineInput f (tmp + ".org");
    return f. getVector ();
  }



  void shellBody () const final
  {
    const string prot            = shellQuote (getArg ("protein"));
    const string dna             = shellQuote (getArg ("nucleotide"));
          string db              =             getArg ("database");
    const bool   update          =             getFlag ("update");
    const string gff             = shellQuote (getArg ("gff"));
    const bool   pgap            =             getFlag ("pgap");
    const double ident           =             arg2double ("ident_min");
    const double cov             =             arg2double ("coverage_min");
    const string organism        = shellQuote (getArg ("organism"));   
    const bool   list_organisms  =             getFlag ("list_organisms");
    const uint   gencode         =             arg2uint ("translation_table"); 
    const bool   add_plus        =             getFlag ("plus");
    const bool   report_common   =             getFlag ("report_common");
    const string mutation_all    =             getArg ("mutation_all");  
          string blast_bin       =             getArg ("blast_bin");
    const string parm            =             getArg ("parm");  
    const string output          = shellQuote (getArg ("output"));
    const bool   quiet           =             getFlag ("quiet");
    const bool   gpipe_org       =             getFlag ("gpipe_org");
    
    
		const string logFName (tmp + ".log");  // Command-local log file


    Stderr stderr (quiet);
    stderr << "Running "<< getCommandLine () << '\n';
  //const Verbose vrb (qc_on);
    
    if (threads_max < threads_max_min)
      throw runtime_error ("Number of threads cannot be less than " + to_string (threads_max_min));
    
		if (ident != -1.0 && (ident < 0.0 || ident > 1.0))
		  throw runtime_error ("ident_min must be between 0 and 1");
		
		if (cov < 0.0 || cov > 1.0)
		  throw runtime_error ("coverage_min must be between 0 and 1");
		  
	  if (report_common && emptyArg (organism))
		  throw runtime_error ("--report_common requires --organism");
		  

		if (! emptyArg (output))
		  try { OFStream f (unQuote (output)); }
		    catch (...) { throw runtime_error ("Cannot open output file " + output); }

    
    // For timing... 
    const time_t start = time (NULL);
    
    
    const size_t threads_max_max = get_threads_max_max ();
    if (threads_max > threads_max_max)
    {
      stderr << "The number of threads cannot be greater than " << threads_max_max << " on this computer" << '\n'
             << "The current number of threads is " << threads_max << ", reducing to " << threads_max_max << '\n';
      threads_max = threads_max_max;
    }


    const string defaultDb (
      #ifdef DEFAULT_DB_DIR
        DEFAULT_DB_DIR "/latest"
      #else
        execDir + "data/latest"
      #endif
      );
      

		// db
		if (db. empty ())
		{
    	if (const char* s = getenv ("AMRFINDER_DB"))
    		db = string (s);
    	else
			  db = defaultDb;
		}
		ASSERT (! db. empty ());		  


		if (update)
    {
      // PD-2447
      if (! emptyArg (prot) || ! emptyArg (dna))
        throw runtime_error ("AMRFinder -u/--update option cannot be run with -n/--nucleotide or -p/--protein options");
      if (! getArg ("database"). empty ())
        throw runtime_error ("AMRFinder update option (-u/--update) only operates on the default database directory. The -d/--database option is not permitted");
      if (getenv ("AMRFINDER_DB"))
      {
        cout << "WARNING: AMRFINDER_DB is set, but AMRFinder auto-update only downloads to the default database directory" << endl;
        db = defaultDb;
      }
  		const Dir dbDir (db);
      if (! dbDir. items. empty () && dbDir. items. back () == "latest")
      {
        findProg ("amrfinder_update");	
  		  exec (fullProg ("amrfinder_update") + " -d " + dbDir. getParent () + ifS (quiet, " -q") + ifS (qc_on, " --debug") + " > " + logFName, logFName);
      }
      else
        cout << "WARNING: Updating database directory works only for databases with the default data directory format." << endl
             << "Please see https://github.com/ncbi/amr/wiki for details." << endl
             << "Current database directory is: " << strQuote (dbDir. get ()) << endl
             << "New database directories will be created as subdirectories of " << strQuote (dbDir. getParent ()) << endl;
		}


    const string downloadLatestInstr ("\nTo download the latest version to the default directory run amrfinder -u");
    
		if (! directoryExists (db))  // PD-2447
		  throw runtime_error ("No valid AMRFinder database found." + ifS (! update, downloadLatestInstr));


    if (list_organisms)
    {
      const StringVector organisms (db2organisms (db));
      cout << "Possible organisms: " + organisms. toString (", ") << endl;
      return;
    }    		  

		  
		// PD-3051
		try
		{
  	  istringstream versionIss (version);
  		const SoftwareVersion softwareVersion (versionIss);
  		const SoftwareVersion softwareVersion_min (db + "/database_format_version.txt");
  	  stderr << "Software version: " << softwareVersion. str () << '\n'; 
  		const DataVersion dataVersion (db + "/version.txt");
  		istringstream dataVersionIss (DATA_VER_MIN); 
  		const DataVersion dataVersion_min (dataVersionIss);  
      stderr << "Database version: " << dataVersion. str () << '\n';
      if (softwareVersion < softwareVersion_min)
        throw runtime_error ("Database requires sofware version at least " + softwareVersion_min. str ());
      if (dataVersion < dataVersion_min)
        throw runtime_error ("Software requires database version at least " + dataVersion_min. str ());
    }
    catch (const exception &e)
    {
      throw runtime_error (e. what () + downloadLatestInstr);
    }


    string searchMode;
    StringVector includes;
    if (emptyArg (prot))
      if (emptyArg (dna))
      {
        if (update)
          return;
	  	  throw runtime_error ("Parameter --prot or --nucleotide must be present");
  		}
      else
      {
    		if (! emptyArg (gff))
          throw runtime_error ("Parameter --gff is redundant");
        searchMode = "translated nucleotide";
      }
    else
    {
      searchMode = "protein";
      if (emptyArg (dna))
      {
        searchMode += "-only";
        includes << key2shortHelp ("nucleotide") + " and " + key2shortHelp ("gff") + " options to add translated searches";
      }
      else
      {
    		if (emptyArg (gff))
          throw runtime_error ("If parameters --prot and --nucleotide are present then parameter --gff must be present");
        searchMode = "combined translated and protein";
      }
    }
    ASSERT (! searchMode. empty ());
    if (emptyArg (organism))
      includes << key2shortHelp ("organism") + " option to add mutation searches and suppress common proteins";
    else
      searchMode += " and mutation";
      

    stderr << "AMRFinder " << searchMode << " search with database " << db;
    {
      const string link (file2link (db));
      if (! link. empty ())
        stderr << ": " << link;
    }
    stderr << "\n";
    
    for (const string& include : includes)
      stderr << "  - include " << include << '\n';
      

    // blast_bin
    if (blast_bin. empty ())
    	if (const char* s = getenv ("BLAST_BIN"))
    		blast_bin = string (s);
    if (! blast_bin. empty ())
    {
	    if (! isRight (blast_bin, "/"))
	    	blast_bin += "/";
	    prog2dir ["blastp"] = blast_bin;
	    prog2dir ["blastx"] = blast_bin;
	    prog2dir ["blastn"] = blast_bin;
	  }
	  
	  // organism --> organism1
	  string organism1;
	  bool suppress_common = false;	  
	  if (! emptyArg (organism))
	  {
	  	organism1 = unQuote (organism);
 	  	replace (organism1, ' ', '_');
 	  	ASSERT (! organism1. empty ());
      if (gpipe_org)
      {
        LineInput f (db + "/taxgroup.tab");
        Istringstream iss;
        bool found = false;
        while (f. nextLine ())
        {
	  	    if (isLeft (f. line, "#"))
	  	      continue;
          iss. reset (f. line);
          string org, gpipeOrg;
          int num = -1;
          iss >> org >> gpipeOrg >> num;
          QC_ASSERT (! org. empty ());
          QC_ASSERT (num >= 0);
          QC_ASSERT (iss. eof ());
          if (organism1 == gpipeOrg)
          {
            organism1 = org;
            found = true;
            break;
          }
        }
        if (! found)
          organism1. clear ();
      }
      if (! organism1. empty ())
      {
        const StringVector organisms (db2organisms (db));
        if (! organisms. contains (organism1))
          throw runtime_error ("Possible organisms: " + organisms. toString (", "));
      }
 	  }
	  if (! organism1. empty ())
 	  	if (! report_common)
 	  	  suppress_common = true;
 	  ASSERT (! contains (organism1, ' '));


    const string qcS (qc_on ? " -qc" : "");
		const string force_cds_report (! emptyArg (dna) && ! organism1. empty () ? "-force_cds_report" : "");  // Needed for dna_mutation
		
								  
    findProg ("fasta_check");
    findProg ("fasta2parts");
    findProg ("amr_report");	
    
    
    string amr_report_blastp;	
 		string amr_report_blastx;
	  const string pgapS (ifS (pgap, " -pgap"));
 		bool blastxChunks = false;
    {
      Threads th (threads_max - 1, true);  
      
      double prot_share = 0.0;
      double dna_share  = 0.0;
  		if ( ! emptyArg (prot))
  		  prot_share = 1.0;  // PAR
  		if (! emptyArg (dna))
  		  dna_share = 1.0;   // PAR
  		const double total_share = prot_share + dna_share;
  		
  		string dna_ = dna;
    #if 0
  		if (! emptyArg (dna) && gpipe)
  		{
  		  dna_ = tmp + ".dna";
  		  if (system (("sed 's/^>gnl|[^|]*|/>/1' " + dna + " > " + dna_). c_str ()))
  		    throw runtime_error ("Cannot remove 'gnl|...|' from " + dna);
  		}
    #endif
  		if (! emptyArg (dna) && pgap)
  		{
  		  dna_ = tmp + ".dna";
  		  if (system (("sed 's/^>lcl|/>/1' " + dna + " | sed 's/^>gnl|/>/1' > " + dna_). c_str ()))
  		    throw runtime_error ("Cannot remove 'lcl|' and 'gnl|' from " + dna);
  		}
  		
  		
  		#define BLAST_FMT  "-outfmt '6 qseqid sseqid qstart qend qlen sstart send slen qseq sseq'"
			  // length nident 

  		
  		// PD-2967
  		const string blastp_par ("-show_gis  -comp_based_stats 0  -evalue 1e-10  ");
  		  // was: -culling_limit 20  // PD-2967
  		const string blastx_par (blastp_par + "  -word_size 3  -seg no  -max_target_seqs 10000  -query_gencode ");
  		if (! emptyArg (prot))
  		{
  			findProg ("blastp");  			
  			findProg ("hmmsearch");
  			
  		  exec (fullProg ("fasta_check") + prot + " -aa -hyphen " + qcS + " -log " + logFName, logFName);  
  			
  			string gff_match;
  			if (! emptyArg (gff) && ! contains (parm, "-bed"))
  			{
  			  string locus_tag;
  			  const int status = system (("grep '^>.*\\[locus_tag=' " + prot + " > /dev/null"). c_str ());
  			  const bool locus_tagP = (status == 0);
  			  if (locus_tagP /*|| gpipe*/)
  			  {
  			    locus_tag = " -locus_tag " + tmp + ".match";
  			    gff_match = " -gff_match " + tmp + ".match";
  			  }
  			  findProg ("gff_check");		
  			  string dnaPar;
  			  if (! emptyArg (dna))
  			    dnaPar = " -dna " + dna_;
  			//const string gpipeS (ifS (gpipe, " -gpipe"));
  			  try 
  			  {
  			    exec (fullProg ("gff_check") + gff + " -prot " + prot + dnaPar /*+ gpipeS*/ + pgapS + locus_tag + qcS + " -log " + logFName, logFName);
  			  }
  			  catch (...)
  			  {
  			    throw runtime_error ("GFF file mismatch.\nMore information in " + logFName);  // PD-3289
  			  } 
  			}
  			
  			if (! fileExists (db + "/AMRProt.phr"))
  				throw runtime_error ("BLAST database " + shellQuote (db + "/AMRProt") + " does not exist");
  			
  			const size_t prot_threads = (size_t) floor ((double) th. getAvailable () * (prot_share / total_share) / 2.0);

  			stderr << "Running blastp...\n";
  			// " -task blastp-fast -word_size 6  -threshold 21 "  // PD-2303
  			string num_threads;
  			if (blastThreadable ("blastp", logFName) && prot_threads > 1)
  			  num_threads = " -num_threads " + to_string (prot_threads);
  			th. exec (fullProg ("blastp") + " -query " + prot + " -db " + db + "/AMRProt  " 
  			  + blastp_par + num_threads + " " BLAST_FMT " -out " + tmp + ".blastp > /dev/null 2> /dev/null", prot_threads);
  			  
  			stderr << "Running hmmsearch...\n";
  			string cpu;
  			if (prot_threads > 1)
  			  cpu = "--cpu " + to_string (prot_threads);
  			th. exec (fullProg ("hmmsearch") + " --tblout " + tmp + ".hmmsearch  --noali  --domtblout " + tmp + ".dom  --cut_tc  -Z 10000  " + cpu + " " + db + "/AMR.LIB " + prot + " > /dev/null 2> /dev/null", prot_threads);

  		  amr_report_blastp = "-blastp " + tmp + ".blastp  -hmmsearch " + tmp + ".hmmsearch  -hmmdom " + tmp + ".dom";
  			if (! emptyArg (gff))
  			  amr_report_blastp += "  -gff " + gff + gff_match;
  		}  		
  		
  		if (! emptyArg (dna))
  		{
  			stderr << "Running blastx...\n";
  			findProg ("blastx");

  		  exec (fullProg ("fasta_check") + dna_ + " -hyphen  -len "+ tmp + ".len " + qcS + " -log " + logFName, logFName); 
  		  const size_t threadsAvailable = th. getAvailable ();
  		//ASSERT (threadsAvailable);
  		  if (threadsAvailable >= 2)
  		  {
    		  exec ("mkdir " + tmp + ".chunk");
    		  exec (fullProg ("fasta2parts") + dna_ + " " + to_string (threadsAvailable) + " " + tmp + ".chunk " + qcS + " -log " + logFName, logFName);   // PAR
    		  exec ("mkdir " + tmp + ".blastx_dir");
    		  FileItemGenerator fig (false, true, tmp + ".chunk");
    		  string item;
    		  while (fig. next (item))
      			th << thread (exec, fullProg ("blastx") + "  -query " + tmp + ".chunk/" + item + " -db " + db + "/AMRProt  "
      			  + blastx_par + to_string (gencode) + " " BLAST_FMT
      			  " -out " + tmp + ".blastx_dir/" + item + " > /dev/null 2> /dev/null", string ());
    		  blastxChunks = true;
  		  }
  		  else
    			th. exec (fullProg ("blastx") + "  -query " + dna_ + " -db " + db + "/AMRProt  "
    			  + blastx_par + to_string (gencode) + " " BLAST_FMT
    			  " -out " + tmp + ".blastx > /dev/null 2> /dev/null", threadsAvailable);
  		  amr_report_blastx = "-blastx " + tmp + ".blastx  -dna_len " + tmp + ".len";
  		}

  		if (   ! emptyArg (dna) 
  		    && ! organism1. empty ()
  		    && fileExists (db + "/AMR_DNA-" + organism1)
  		   )
  		{
  			findProg ("blastn");
  			findProg ("dna_mutation");
  			stderr << "Running blastn...\n";
  			exec (fullProg ("blastn") + " -query " + dna_ + " -db " + db + "/AMR_DNA-" + organism1 + " -evalue 1e-20  -dust no  "
  			  BLAST_FMT " -out " + tmp + ".blastn > " + logFName + " 2> " + logFName, logFName);
  		}
  	}
  	
  	
  	if (blastxChunks)
  	  exec ("cat " + tmp + ".blastx_dir/* > " + tmp + ".blastx");
  	  
  	
  	if (suppress_common)
			exec ("set +o pipefail && grep -v '^#' " + db + "/AMRProt-suppress | grep -w ^" + organism1 + " | cut -f 2 > " + tmp + ".suppress_prot"); 
		

    // ".amr"
    const string mutation_allS (mutation_all. empty () ? "" : ("-mutation_all " + mutation_all));
    const string coreS (add_plus ? "" : " -core");
		exec (fullProg ("amr_report") + " -fam " + db + "/fam.tab  " + amr_report_blastp + "  " + amr_report_blastx
		  + "  -organism " + strQuote (organism1) + "  -mutation " + db + "/AMRProt-mutation.tab " + mutation_allS + " "
		  + force_cds_report + " -pseudo" + coreS
		  + (ident == -1 ? string () : "  -ident_min "    + toString (ident)) 
		  + "  -coverage_min " + toString (cov)
		  + ifS (suppress_common, " -suppress_prot " + tmp + ".suppress_prot") + pgapS
		  + qcS + " " + parm + " -log " + logFName + " > " + tmp + ".amr", logFName);
		if (   ! emptyArg (dna) 
		    && ! organism1. empty ()
		    && fileExists (db + "/AMR_DNA-" + organism1)
		   )
		{
			exec (fullProg ("dna_mutation") + tmp + ".blastn " + db + "/AMR_DNA-" + organism1 + ".tab" + qcS + " -log " + logFName + " > " + tmp + ".amr-snp", logFName);
			exec ("tail -n +2 " + tmp + ".amr-snp >> " + tmp + ".amr");
	  }

    // Sorting
    // PD-2244, PD-3230
    const string sortS (emptyArg (dna) && emptyArg (gff) ? "-k1,1 -k2,2" : "-k2,2 -k3,3n -k4,4n -k5,5 -k1,1");
		exec ("head -1 "              + tmp + ".amr                      >  " + tmp + ".amr-out");
		exec ("LANG=C && tail -n +2 " + tmp + ".amr | sort " + sortS + " >> " + tmp + ".amr-out");
		exec ("mv " + tmp + ".amr-out " + tmp + ".amr");

		
    // timing the run
    const time_t end = time (NULL);
    stderr << "AMRFinder took " << end - start << " seconds to complete\n";


		if (emptyArg (output))
		  exec ("cat " + tmp + ".amr");
		else
		  exec ("cp " + tmp + ".amr " + output);
  }
};



}  // namespace




int main (int argc, 
          const char* argv[])
{
  ThisApplication app;
  return app. run (argc, argv);  
}




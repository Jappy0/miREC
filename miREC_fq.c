#include <cstring>
#include <string>
#include <fstream>
#include <algorithm>
#include <vector>
#include <iostream>
#include <unistd.h>
#include "omp.h"

using namespace std;

const char invert_code_rule[4] = {'A', 'C', 'G', 'T'}; //decoding rule
const int MAX_CHAR_NUM = 1<<20;
const char* inbase = "ATCG";

unsigned int K_value;
unsigned int t_FN;
int r_FN;
std::string m_FN;
std::string l_FN;
std::string f_FN;

ofstream outfile("correct_read.fastq");
ofstream outlistfile("ID_read_quality_cor.txt");
ofstream correctedfile("changed_list.txt");//[ID  Changed_read] as input file for check.c
ofstream correcteddetailfile("changed_detail.txt");

hash<string> str_hash;
int size_str_hash = MAX_CHAR_NUM; //2^30

struct KmerNode
{
    string kmer;
    int freq;
};

//store info from k-mer frequency file
vector<KmerNode> *kmerfreq; //2^30

//store info from (k+1)mer frequency file
vector<KmerNode> *kmer_a; //2^30

//store info from (k-1)mer frequency file
vector<KmerNode> *kmer_m; //2^30

//store info from read frequency file
vector<KmerNode> *readfreq; //2^30

//store info from fastq file,includes ID,raw read, phreh quality
vector<string> F_id;
vector<string> F_read;
vector<string> F_quality;
vector<int> F_flag;

vector<int> Err_posi;
vector<int> Err_posi_tmp;
vector<int> Freq_b;
vector<int> Freq_a;

std::ostream *v1logger;

//setting parameters, refering to read copy at least 6 in simu_datasets
//int low_setting = 5; //kmer freqency < [], may contains errors,store in a parameter(r_FN)
int errread_setting =5; //errornuos reads from copy number < [];
int changeread_setting =5; //after correct, read copy > [], confirm this correction.


//show help
inline void displayHelp(const char* prog) {
	printf("microRNAEC v1.1, by XuanZhang, March 2020.\n");
	printf("Usage: %s -k <k_value> -m <kmerfrequency_fileName> -l <real_explevel_fileName> -f <fastq_fileName>[option parameters]\n", prog);
	printf("\t options:\n \t\t -l <length> -k <length of k-mer> -s <strands> -t <threads>\n\n");
	// printf("-----------\n");
	printf("\t\t -k is the k_value, for k-mer extraceted\n");
	printf("\t\t -m is the related k-mer frequency file name\n");
	printf("\t\t -l is the current read expression-level file name\n");
	printf("\t\t -f is the raw read info file from fastq\n");
	printf("\t\t -t is the number of threads\n");
	printf("\t\t -r is the value of frequency threshold\n");
	
	printf("Example:\n\t\t");
	printf("./miREC_update -k 7 -t 16  -m 7.freq  -r 5 -l 18-25_expreLevel.txt -f ID_read_quality.txt\n\n");
}

//show current params
inline void displayParams() {
	printf("k_value is k = %d\n", K_value);
	printf("the number of threads is t = %d\n", t_FN);
	printf("the value of frequency threshold is r = %d\n", r_FN);
	printf("k-mer frequency file is: %s\n", m_FN.c_str());
	printf("read expression-level file is: %s\n", l_FN.c_str());
	printf("raw read info file is: %s\n", f_FN.c_str());
}

//get params
inline void getPars(int argc, char* argv[]) {
	v1logger = &std::cout;
	int oc;
	while ((oc = getopt(argc, argv, "k:m:l:f:t:r:hf")) >= 0) {
		switch (oc) {
			case 'k':
				K_value = atoi(optarg);
				break;
			case 'm':
				m_FN = optarg;
				break;
			case 'l':
				l_FN = optarg;
				break;
			case 'f':
				f_FN = optarg;
				break;
			case 't':
				t_FN = atoi(optarg);
				break;
			case 'r':
				r_FN = atoi(optarg);
				break;				
			case 'h':
				displayHelp(argv[0]);
				exit(0);			
			case '?':
				std::cerr << "Error parameters.\n Please run 'miREC -h'\n";
				exit(1);
				break;
		}
	}

	std::ifstream f;

	f.open(m_FN);
	if (f.fail()) {
		fprintf(stderr, "k-mer frequency file  '%s' does not exist.\n", m_FN.c_str());
		exit(1);
	}
	f.close();

	f.open(l_FN);
	if (f.fail()) {
		fprintf(stderr, "read expression-level file '%s' does not exist.\n", l_FN.c_str());
		exit(1);
	}
	f.close();

	f.open(f_FN);
	if (f.fail()) {
		fprintf(stderr, "raw read info file '%s' does not exist.\n", f_FN.c_str());
		exit(1);
	}
	f.close();

}

//setting parameters, refering to read copy at least 6 in simu_datasets
inline void initial() {
	//nothing here
	kmerfreq = new vector<KmerNode>[MAX_CHAR_NUM];
	kmer_a = new vector<KmerNode>[MAX_CHAR_NUM];
	kmer_m = new vector<KmerNode>[MAX_CHAR_NUM];
	readfreq = new vector<KmerNode>[MAX_CHAR_NUM];
}

//string hash: transfer mer/read to integer
int str_hash_index(string s_seq)
{
    //upperCase
    transform(s_seq.begin(), s_seq.end(), s_seq.begin(), ::toupper);
    
    size_t b = str_hash(s_seq);

    int index = b % size_str_hash;

    return index;
}

//read freq_FIle
void readfreq_File(const char *refFile, vector<KmerNode> *kmerfreq) { // processing reference file

    FILE *fp = fopen(refFile, "r");
    if (NULL == fp) {
        printf("fail to open file %s\n", refFile);
        return;
    }

    int r_count = 0;
   
    int freq;
    char *kmer;
    kmer = new char[MAX_CHAR_NUM];
    int hash_index;

    KmerNode node;
    while (fscanf(fp, "%s %d",kmer, &freq) != EOF) {   
        
        node.kmer = kmer;
        node.freq = freq;

        hash_index = str_hash_index(kmer);
        kmerfreq[hash_index].push_back(node);

        r_count++;
        
    }
    fclose(fp);
    cout<<r_count<<endl;
}

//read fastq :ID_read_quality.txt(preprocess from row_fastq file)
void readrow_fastqFile(const char *refFile) { // processing reference file

	FILE *fp = fopen(refFile, "r");
	if (NULL == fp) {
		printf("fail to open file %s\n", refFile);
		return;
	}

	char *id, *read, *quality;
	id = new char[MAX_CHAR_NUM];
	read = new char[MAX_CHAR_NUM];
	quality = new char[MAX_CHAR_NUM];	

	while (fscanf(fp, "%s %s %s",id, read, quality) != EOF) {
			F_id.push_back(id);
			F_read.push_back(read);
			F_flag.push_back(0);
			Freq_b.push_back(0);
			Freq_a.push_back(0);
			Err_posi.push_back(0);
			Err_posi_tmp.push_back(0);
			F_quality.push_back(quality);		
	}
	fclose(fp);
}

//read fasta :ID_read_quality.txt(preprocess from row_fastq file)
void readrow_fastaFile(const char *refFile) { // processing reference file

	FILE *fp = fopen(refFile, "r");
	if (NULL == fp) {
		printf("fail to open file %s\n", refFile);
		return;
	}

	char *id, *read;
	id = new char[MAX_CHAR_NUM];
	read = new char[MAX_CHAR_NUM];
	
	while (fscanf(fp, "%s %s",id, read) != EOF) {
			F_id.push_back(id);
			F_read.push_back(read);
			F_flag.push_back(0);
			F_quality.push_back("novalue");
	}
	fclose(fp);
}

//For each string kmer, find alperbately smaller k-mer
string small_kmer(string lar){

 	string smal;
 	transform(lar.begin(), lar.end(), lar.begin(), ::toupper);
 	smal = lar;
	
 	int rindex = lar.length()-1;

 	for (int i= rindex; i >= 0; i--)
 	{	
 		switch(lar[i])
 		{
 			case'A': smal[rindex-i] = 'T'; break;
 			case'T': smal[rindex-i] = 'A'; break;
 			case'C': smal[rindex-i] = 'G'; break;
 			case'G': smal[rindex-i] = 'C'; break;
 			case'N': smal[rindex-i] = lar[rindex]; break;

 		}
 	}

 	if (lar.compare(smal) > 0)
 	{
 		return smal;
 	}
 	else
 	{
 		return lar;
 	}
}

//For each string kmer, find reverse_complement one
string rever_comp(string lar){

 	string smal;
 	transform(lar.begin(), lar.end(), lar.begin(), ::toupper);
 	smal = lar;
	
 	int rindex = lar.length()-1;

 	for (int i= rindex; i >= 0; i--)
 	{	
 		switch(lar[i])
 		{
 			case'A': smal[rindex-i] = 'T'; break;
 			case'T': smal[rindex-i] = 'A'; break;
 			case'C': smal[rindex-i] = 'G'; break;
 			case'G': smal[rindex-i] = 'C'; break;
 			case'N': smal[rindex-i] = lar[rindex]; break;

 		}
 	}

 	return smal;
}

//check whether errorous read. return freq_value
int read_expresscheck(string read){

	int hash_index = str_hash_index(read);
    for (unsigned int i =0; i < readfreq[hash_index].size(); i++)
    {
    	if( readfreq[hash_index][i].kmer == read )
    	{
    		//return read_freq
			return readfreq[hash_index][i].freq;
    	}	
    }
    //read doesn't appear in raw data
    return 0;
}

//check whether errorous kmer
int low_kmercheck(string read){

	int hash_index = str_hash_index(read);
    for (unsigned int i =0; i < kmerfreq[hash_index].size(); i++)
    {
    	if( kmerfreq[hash_index][i].kmer == read )
    	{
    		if( kmerfreq[hash_index][i].freq <= r_FN )
    		{
				//might have protential errors
				return 1;
    		}
    		else
    		{
    			//no protential error in the read
				return 0;
    		}
    	}	
    }
    return 1;
}

//find candidate correct kmer
string find_cankmer(string mer, int readID, int kmer_posi){

	string kmer_cor;
	string tmp_cor = "Nofound";
	int tmp_freq = 0, tmp_posi =0;

	for(unsigned int i=0; i<mer.size(); i++)
	{	
		kmer_cor = mer;
		for(int j=0; j<4; j++)
		{
			if(mer[i] != invert_code_rule[j])
			{
				kmer_cor[i] = invert_code_rule[j];

				int hash_index = str_hash_index(kmer_cor);
			    for (unsigned int i =0; i < kmerfreq[hash_index].size(); i++)
			    {
			    	if( kmerfreq[hash_index][i].kmer == kmer_cor )
			    	{
				 		//"find a correct candidate,which freq is not 1

				 		if( kmerfreq[hash_index][i].freq > tmp_freq )
				 		{
				 			tmp_freq = kmerfreq[hash_index][i].freq;
				 			tmp_cor = kmer_cor;
				 			tmp_posi = i + kmer_posi + 1; //the number of base in a read

				 		}
				 	}	    			
			    }
			}
		}
	}
	Err_posi_tmp[readID] = tmp_posi;
	return tmp_cor;
}

int main(int argc, char *argv[]) {
	
	if (argc < 3) {
		displayHelp(argv[0]);
		return 1;
	}
	
	getPars(argc, argv);

	displayParams();
	initial();
	
	cout<<"total kmer: ";
	readfreq_File(m_FN.c_str(), kmerfreq);

	cout<<"total unique read: ";
	readfreq_File(l_FN.c_str(), readfreq);

	readrow_fastqFile(f_FN.c_str());

	//write correct reads
	if(!outfile){
   	cout << "Unable to open otfile";
        exit(1); // terminate with error

	}
	
	int schange = 0, nochange = 0;
	//cout << "thread number is"<<t_FN<<endl;
	omp_set_num_threads(t_FN);
	//For each reads from FASTQ, check if its express low, check k-mer futher
	#pragma omp parallel for
	for (unsigned int i = 0; i < F_read.size(); ++i)
	{
		string tmp,mer,small,reverse;
		unsigned int read_len=0;
		int tmp_readf_b = 0, tmp_readf_a =0;
		tmp_readf_b = read_expresscheck(F_read.at(i));
		if( tmp_readf_b < errread_setting)
		{
 			//this read exist errors and need to be corrected	
 			read_len=F_read.at(i).size();

 			//for each k-mer in the read
 			if (read_len >= K_value)
 			{
				//check each kmer in a read
	 			string check_read;
	 			string corkmer;
	 			int Flag = 0;
	 			for (unsigned int j=0; j< (read_len - K_value+1); j++)
	 			{
	 				tmp = F_read.at(i);
	 				Flag = 0; 
	 				mer.assign(tmp,j,K_value);
	 				small = small_kmer(mer);
	 				if (mer != small)
	 				{
	 					//original k-mer is not smaller k-mer,need to revers_comp,flag =1
	 					Flag = 1;
					}

					//==1, the kmer with low frequency and might be errorous
					if (low_kmercheck(small) == 1)
					{	
						//find_cankmer
						corkmer = find_cankmer(small,i,j);
						if ( corkmer != "Nofound" )
						{
							//替换
		 					//start correction
		 					if(Flag ==0)
		 					{
		 						check_read = tmp;
		 						check_read.replace(j, K_value, corkmer);
		 						//check freq of revised read
		 						tmp_readf_a = read_expresscheck(check_read);
		 						if( tmp_readf_a > changeread_setting ){ 						

		 							F_read.at(i).replace(j, K_value, corkmer);
		 							// readfreqs_change++;
		 							__sync_fetch_and_add(&schange, 1);
		 							
		 							F_flag.at(i) = 1;
		 							Err_posi[i] = Err_posi_tmp[i];
		 							Freq_b[i] = tmp_readf_b;
		 							Freq_a[i] = tmp_readf_a;		 			
		 							continue;
		 						}
		 						else{
		 							//cout<<"creat a new read------ "<<endl;
		 							__sync_fetch_and_add(&nochange, 1);
		 						}	
		 
		 					}
		 					else//反链修改
		 					{

		 						check_read = tmp;
		 						reverse = rever_comp(corkmer);
		 						check_read.replace(j, K_value,reverse);
		 						//cout<<"改后的链"<<F_read.at(i)<<endl;
		 						tmp_readf_a = read_expresscheck(check_read);
		 						if( tmp_readf_a > changeread_setting ){ 
		 							F_read.at(i).replace(j, K_value,reverse);
		 							// readfreqs_change++;
		 							__sync_fetch_and_add(&schange, 1);
		 							//cout<<"read : "<<F_read.at(i)<<endl;

		 							F_flag.at(i) = 1;
		 							Err_posi[i] = Err_posi_tmp[i];
		 							Freq_b[i] = tmp_readf_b;
		 							Freq_a[i] = tmp_readf_a;		 							
		 							continue;
		 						}
		 						else{
		 							//don't change the reads, cause read frequency doesn't change
		 							__sync_fetch_and_add(&nochange, 1);
		 						}
					
		 					}
						}
					}
				}
			}
		}
	}

	for (unsigned int i = 0; i < F_read.size(); ++i)
	{
		//for fastq file
		outfile<<F_id.at(i)<<endl<<F_read.at(i)<<endl<<"+"<<endl<<F_quality.at(i)<<endl; 
		outlistfile<<F_id.at(i)<<" "<<F_read.at(i)<<" "<<F_quality.at(i)<<endl;
		
		if(F_flag.at(i) == 1)
		{
			correctedfile<<F_id.at(i)<<" "<<F_read.at(i)<<endl;
			correcteddetailfile<<F_id.at(i)<<" "<<F_read.at(i)<<" sub "<<Err_posi[i]<<" "<<Freq_b[i]<<" "<<Freq_a[i]<<endl;
		}
	}

	cout<<"the number of correced subs ： "<<schange<<endl<<endl;

	outfile.close();
	outlistfile.close();
	correctedfile.close();
	correcteddetailfile.close();
}

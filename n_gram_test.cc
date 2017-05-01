#include <iostream>
#include <regex>
#include "sqlite3.h"
#ifndef SQLITE_MAX_COLUMN
#define SQLITE_MAX_COLUMN 2000//sqlite3.h doesn't define it? usually should be 2000?
#endif
#define NO_NEED_TO_AVERT_RACES
#include "cpprs.h"
#include "cpprs_sqlite_mod.h"
#define LOG_Is(i) {STD_CLOG_FILE_FUNC_LINE<<i<<'\n'<<std::flush;}
#define LOG_I_CIFNOTNULLSQLITEFREEs(i,c) {STD_CLOG_FILE_FUNC_LINE<<i;if(c){clog<<' '<<c;sqlite3_free(c);}clog<<'\n'<<std::flush;}
#include "sqlite3minwrapper.h"

using namespace std;

//note this examine_n_gram doesn't work when constant NGRAM is big
template<class T0,class T1,class T2,class T3,class T4,class T5,class T6,class T7,class T8>
void examine_n_gram(T0 sumofngr2,T1 ngfre,T2 numofgrtouse,T3 seofgrend,T4 segrsiend,T5 segrfrend,T6 sbptr,T7 lenofs,T8 freofs){
	//?it seems saner to change 0x4000 and 0x100 to 0x8000 and 0x800?
	//if((uintmax_t)sumofngr2/(ngfre+1)>nthpowerof(0x400,numofgrtouse) || accumulate(segrfrend-numofgrtouse,segrfrend,(uintmax_t)freofs)/(numofgrtouse+1)/(ngfre+1)>nthpowerof(0x100,numofgrtouse))//fixme unsafe average
	if((uintmax_t)sumofngr2/(ngfre+1)>nthpowerof(0x4000,numofgrtouse) || (uintmax_t)min(*min_element(segrfrend-numofgrtouse,segrfrend),freofs)/(ngfre+1)>nthpowerof(0x100,numofgrtouse)){
		cout<<"UNCOMMON SEQUENCE: ";
		//remove cout<<sumofngr2<<'\n'<<ngfre<<'\n'<<numofgrtouse<<'\n'<<min(*min_element(segrfrend-numofgrtouse,segrfrend),freofs)<<'\n';
		for(int i=-numofgrtouse;;){
			cout.write(seofgrend[i],segrsiend[i]);
			cout<<' ';
			++i;
			if(!i)break;
		}
		cout.write(sbptr,lenofs);
		cout<<'\n';
	}
}

static void errorLogCallback(void *pArg, int iErrCode, const char *zMsg)noexcept{
	try{
		STD_CLOG_FILE_FUNC_LINE<<"SQLite: "<<iErrCode<<' '<<zMsg<<'\n'<<std::flush;
	}catch(...){}
}
int main(int argc, char *argv[]){
#define NGRAM 4
	try{
		sqlite3 *psl=nullptr;
		char *zErrMsg = nullptr;
		auto ib=sqlite3_config(SQLITE_CONFIG_SINGLETHREAD);
		if(SQLITE_OK!=ib){LOG_Is(ib) throw 0;}
		ib=sqlite3_config(SQLITE_CONFIG_LOG, errorLogCallback, NULL);
		if(SQLITE_OK!=ib){LOG_Is(ib) throw 0;}
		ib=sqlite3_open_v2("n_gram_database", &psl,SQLITE_OPEN_READONLY,NULL);
		if(SQLITE_OK!=ib){LOG_Is(ib) throw 0;}
		ib=sqlite3_exec(psl, "PRAGMA locking_mode = EXCLUSIVE", NULL, NULL, &zErrMsg);
		if(SQLITE_OK!=ib){LOG_I_CIFNOTNULLSQLITEFREEs(ib,zErrMsg) throw 0;}
		ib=sqlite3_exec(psl, "PRAGMA synchronous = OFF", NULL, NULL, &zErrMsg);
		if(SQLITE_OK!=ib){LOG_I_CIFNOTNULLSQLITEFREEs(ib,zErrMsg) throw 0;}
		ib=sqlite3_exec(psl, "PRAGMA journal_mode = OFF", NULL, NULL, &zErrMsg);//when it's OFF, rollback cannot be used.
		if(SQLITE_OK!=ib){LOG_I_CIFNOTNULLSQLITEFREEs(ib,zErrMsg) throw 0;}
		ib=sqlite3_exec(psl, "PRAGMA cache_size = 90000000", NULL, NULL, &zErrMsg);//90GB in-memory (with default page_size)
		if(SQLITE_OK!=ib){LOG_I_CIFNOTNULLSQLITEFREEs(ib,zErrMsg) throw 0;}
		ib=sqlite3_exec(psl, "PRAGMA temp_store = MEMORY", NULL, NULL, &zErrMsg);
		if(SQLITE_OK!=ib){LOG_I_CIFNOTNULLSQLITEFREEs(ib,zErrMsg) throw 0;}
		ib=sqlite3_exec(psl, "PRAGMA automatic_index = 0", NULL, NULL, &zErrMsg);//main reason to turn this off is that my WITH clause causes (284) automatic index warning!
		if(SQLITE_OK!=ib){LOG_I_CIFNOTNULLSQLITEFREEs(ib,zErrMsg) throw 0;}
		sqlite3_int64 sumofngr2;
		INIT_TRYs(slw_prestmt sstmt(psl, LITERAL_COMMA_SIZE("select sum(fre) from ngr2"));)
		ib=sqlite3_step(sstmt.s.pstmt);
		if(SQLITE_ROW!=ib){LOG_Is(ib) throw 0;}
		sumofngr2=sqlite3_column_int64(sstmt.s.pstmt, 0);//note NULL is converted to integer 0
		CATCH_SET_SUE_THROWs(;)
		auto trd=readstreamintovector<1>(cin);
		auto sbptr=trd.data();
		auto septr=sbptr+trd.size()-1;
		constexpr auto lenlongestenglishword=sizeof "Antidisestablishmentarianism"-1;
		//todo add special handling of the slash / appearing as OR in English
		regex wordre{"'?[A-Za-z]['a-z]*(-[A-Z]?['a-z]+)*"};//this regex is valid word, ' ' is special character for sequence, a few characters are boundary, all other characters are separator
		decltype(sbptr) seofgr[NGRAM-1];
		size_t segrsi[NGRAM-1];
		sqlite3_int64 segrfr[NGRAM-1];
		unsigned numofsetgr=0;
		replace(sbptr,septr,'\0','.');//note without replacing you cannot use strpbrk for search
		for(char *smem;sbptr!=septr;sbptr=smem+1){
			//smem=(char *)memchr(sbptr,(unsigned char)' ',septr-sbptr);
			smem=strpbrk(sbptr,"\t\n\r !\"()*+,./:;<=>?[\\]^_`{|}~");//note you might be attempted to add more characters (e.g. unicode) for boundary, but in that case many strange sequences will be inserted to db?
			if(!smem){
				smem=septr;//optimize??
				//break;
			}
			size_t lenofs=smem-sbptr;
			if(lenofs>lenlongestenglishword||!regex_match(sbptr,smem,wordre)){
				if(smem==septr)break;
				numofsetgr=0;
				continue;
			}
			for(auto cosbptr=sbptr;cosbptr!=smem;++cosbptr) *(unsigned char *)cosbptr=rs_sqlite3UpperToLower[*(unsigned char *)cosbptr];
			sqlite3_int64 freofs;
			INIT_TRYs(slw_prestmt sstmt(psl, LITERAL_COMMA_SIZE("select sum(fre) from ngr2 where t0=?1 or t1=?1"));)
			ib=sqlite3_bind_text(sstmt.s.pstmt, 1, sbptr,lenofs, SQLITE_STATIC);
			if(SQLITE_OK!=ib){LOG_Is(ib) throw 0;}
			ib=sqlite3_step(sstmt.s.pstmt);
			if(SQLITE_ROW!=ib){LOG_Is(ib) throw 0;}
			freofs=sqlite3_column_int64(sstmt.s.pstmt, 0);//note NULL is converted to integer 0
			CATCH_SET_SUE_THROWs(;)
			for(unsigned numofgrtouse=1;numofgrtouse<=numofsetgr;++numofgrtouse){
				auto sstring{string{LITERAL_COMMA_LEN("select fre from ngr")}+to_string(numofgrtouse+1)+" where"};
				for(unsigned cngr=0;cngr<=numofgrtouse;++cngr)
					sstring+=" t"+to_string(cngr)+"=? and";
				sstring.resize(sstring.size()-4);
				INIT_TRYs(slw_prestmt sstmt(psl, sstring.c_str(),sstring.size()+1);)
				ib=sqlite3_bind_text(sstmt.s.pstmt, numofgrtouse+1, sbptr,lenofs, SQLITE_STATIC);
				if(SQLITE_OK!=ib){LOG_Is(ib) throw 0;}
				for(unsigned cngr=1;cngr<=numofgrtouse;++cngr){
					ib=sqlite3_bind_text(sstmt.s.pstmt, numofgrtouse-cngr+1, seofgr[numofsetgr-cngr],segrsi[numofsetgr-cngr], SQLITE_STATIC);
					if(SQLITE_OK!=ib){LOG_Is(ib) throw 0;}
				}
				ib=sqlite3_step(sstmt.s.pstmt);
				sqlite3_int64 ngfre;
				switch(ib){
				case SQLITE_ROW:
					ngfre=sqlite3_column_int64(sstmt.s.pstmt, 0);
					break;
				case SQLITE_DONE:
					ngfre=0;
					break;
				default:{LOG_Is(ib) throw 0;}
				}
				examine_n_gram(sumofngr2,ngfre,numofgrtouse,seofgr+numofsetgr,segrsi+numofsetgr,segrfr+numofsetgr,sbptr,lenofs,freofs);
				CATCH_SET_SUE_THROWs(;)
			}
			if(smem==septr)break;
			if(*smem==' '){
				if(numofsetgr==NGRAM-1){
					memmove(seofgr,seofgr+1,sizeof *seofgr*(NGRAM-2));
					memmove(segrsi,segrsi+1,sizeof *segrsi*(NGRAM-2));
					memmove(segrfr,segrfr+1,sizeof *segrfr*(NGRAM-2));
					seofgr[NGRAM-2]=sbptr;
					segrsi[NGRAM-2]=lenofs;
					segrfr[NGRAM-2]=freofs;
				}else{
					seofgr[numofsetgr]=sbptr;
					segrsi[numofsetgr]=lenofs;
					segrfr[numofsetgr]=freofs;
					++numofsetgr;
				}
			}else{
				numofsetgr=0;
			}
		}
		//todo add special key for each training, so duplicate training is avoided? (e.g. the url of training data if it's a web page.)
		ib=sqlite3_close(psl);
		if(SQLITE_OK!=ib){LOG_Is(ib) throw 0;}
		return 0;
	}catch(const std::exception &e){
		try{clog<<e.what()<<'\n'<<std::flush;}catch(...){}
	}catch(...){
		try{clog<<"error\n"<<std::flush;}catch(...){}
	}
	return 1;
}

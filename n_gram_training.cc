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
static void errorLogCallback(void *pArg, int iErrCode, const char *zMsg)noexcept{
	try{
		STD_CLOG_FILE_FUNC_LINE<<"SQLite: "<<iErrCode<<' '<<zMsg<<'\n'<<std::flush;
	}catch(...){}
}
int main(int argc, char *argv[]){
#define NGRAM 4
	static_assert(NGRAM<SQLITE_MAX_COLUMN-0x10,"st");
	try{
		sqlite3 *psl=nullptr;
		char *zErrMsg = nullptr;
		auto ib=sqlite3_config(SQLITE_CONFIG_SINGLETHREAD);
		if(SQLITE_OK!=ib){LOG_Is(ib) throw 0;}
		ib=sqlite3_config(SQLITE_CONFIG_LOG, errorLogCallback, NULL);
		if(SQLITE_OK!=ib){LOG_Is(ib) throw 0;}
		ib=sqlite3_open("n_gram_database", &psl);
		if(SQLITE_OK!=ib){LOG_Is(ib) throw 0;}
		ib=sqlite3_exec(psl, "PRAGMA locking_mode = EXCLUSIVE", NULL, NULL, &zErrMsg);
		if(SQLITE_OK!=ib){LOG_I_CIFNOTNULLSQLITEFREEs(ib,zErrMsg) throw 0;}
		ib=sqlite3_exec(psl, "PRAGMA synchronous = OFF", NULL, NULL, &zErrMsg);
		if(SQLITE_OK!=ib){LOG_I_CIFNOTNULLSQLITEFREEs(ib,zErrMsg) throw 0;}
		ib=sqlite3_exec(psl, "PRAGMA journal_mode = MEMORY", NULL, NULL, &zErrMsg);//when it's OFF, rollback cannot be used.
		if(SQLITE_OK!=ib){LOG_I_CIFNOTNULLSQLITEFREEs(ib,zErrMsg) throw 0;}
		ib=sqlite3_exec(psl, "PRAGMA cache_size = 90000000", NULL, NULL, &zErrMsg);//90GB in-memory (with default page_size)
		if(SQLITE_OK!=ib){LOG_I_CIFNOTNULLSQLITEFREEs(ib,zErrMsg) throw 0;}
		ib=sqlite3_exec(psl, "PRAGMA temp_store = MEMORY", NULL, NULL, &zErrMsg);
		if(SQLITE_OK!=ib){LOG_I_CIFNOTNULLSQLITEFREEs(ib,zErrMsg) throw 0;}
		ib=sqlite3_exec(psl, "PRAGMA automatic_index = 0", NULL, NULL, &zErrMsg);//main reason to turn this off is that my WITH clause causes (284) automatic index warning!
		if(SQLITE_OK!=ib){LOG_I_CIFNOTNULLSQLITEFREEs(ib,zErrMsg) throw 0;}
		string sstr{LITERAL_COMMA_LEN("create table if not exists ngr")};
		auto sstrlen=sstr.size();
		for(unsigned ngr=2;;++ngr){
			sstr+=to_string(ngr)+'(';
			string pkeys;
			for(unsigned cngr=0;cngr<ngr;++cngr){
				auto cngrstr='t'+to_string(cngr);
				sstr+=cngrstr+" text,";
				pkeys+=cngrstr+',';
			}
			sstr.append(LITERAL_COMMA_LEN("fre integer,primary key("));
			sstr.append(pkeys,0,pkeys.size()-1);
			sstr.append("))without rowid");
			ib=sqlite3_exec(psl, sstr.c_str(), NULL, NULL, &zErrMsg);
			if(SQLITE_OK!=ib){LOG_I_CIFNOTNULLSQLITEFREEs(ib,zErrMsg) throw 0;}
			if(ngr==NGRAM)break;
			sstr.resize(sstrlen);
		}
		ib=sqlite3_exec(psl, "BEGIN EXCLUSIVE", NULL, NULL, &zErrMsg);
		if(SQLITE_OK!=ib){LOG_I_CIFNOTNULLSQLITEFREEs(ib,zErrMsg) throw 0;}
		auto trd=readstreamintovector<1>(cin);
		auto sbptr=trd.data();
		auto septr=sbptr+trd.size()-1;
		constexpr auto lenlongestenglishword=sizeof "Antidisestablishmentarianism"-1;
		//todo add special handling of the slash / appearing as OR in English
		regex wordre{"'?[A-Za-z]['a-z]*(-[A-Z]?['a-z]+)*"};//this regex is valid word, ' ' is special character for sequence, a few characters are boundary, all other characters are separator
		decltype(sbptr) seofgr[NGRAM-1];
		size_t segrsi[NGRAM-1];
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
			for(unsigned numofgrtouse=1;numofgrtouse<=numofsetgr;++numofgrtouse){
				auto sstring{string{LITERAL_COMMA_LEN("update ngr")}+to_string(numofgrtouse+1)+" set fre=fre+1 where"};
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
				if(SQLITE_DONE!=ib){LOG_Is(ib) throw 0;}
				CATCH_SET_SUE_THROWs(;)
				if(!sqlite3_changes(psl)){
					sstring.assign(LITERAL_COMMA_LEN("insert into ngr"));
					sstring+=to_string(numofgrtouse+1)+" values(";
					for(unsigned cngr=0;cngr<=numofgrtouse;++cngr) sstring+="?,";
					sstring.append(LITERAL_COMMA_LEN("1)"));
					INIT_TRYs(slw_prestmt sstmt(psl, sstring.c_str(),sstring.size()+1);)
					ib=sqlite3_bind_text(sstmt.s.pstmt, numofgrtouse+1, sbptr,lenofs, SQLITE_STATIC);
					if(SQLITE_OK!=ib){LOG_Is(ib) throw 0;}
					for(unsigned cngr=1;cngr<=numofgrtouse;++cngr){
						ib=sqlite3_bind_text(sstmt.s.pstmt, numofgrtouse-cngr+1, seofgr[numofsetgr-cngr],segrsi[numofsetgr-cngr], SQLITE_STATIC);
						if(SQLITE_OK!=ib){LOG_Is(ib) throw 0;}
					}
					ib=sqlite3_step(sstmt.s.pstmt);
					if(SQLITE_DONE!=ib){LOG_Is(ib) throw 0;}
					CATCH_SET_SUE_THROWs(;)
				}
			}
			if(smem==septr)break;
			if(*smem==' '){
				if(numofsetgr==NGRAM-1){
					memmove(seofgr,seofgr+1,sizeof *seofgr*(NGRAM-2));
					memmove(segrsi,segrsi+1,sizeof *segrsi*(NGRAM-2));
					seofgr[NGRAM-2]=sbptr;
					segrsi[NGRAM-2]=lenofs;
				}else{
					seofgr[numofsetgr]=sbptr;
					segrsi[numofsetgr]=lenofs;
					++numofsetgr;
				}
			}else{
				numofsetgr=0;
			}
		}
		//todo add special key for each training, so duplicate training is avoided? (e.g. the url of training data if it's a web page.)
		ib=sqlite3_exec(psl, "COMMIT", NULL, NULL, &zErrMsg);
		if(SQLITE_OK!=ib){LOG_I_CIFNOTNULLSQLITEFREEs(ib,zErrMsg) throw 0;}
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

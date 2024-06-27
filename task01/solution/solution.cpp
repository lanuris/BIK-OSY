#ifndef __PROGTEST__
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <string>
#include <vector>
#include <array>
#include <iterator>
#include <set>
#include <list>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <compare>
#include <queue>
#include <stack>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <condition_variable>
#include <pthread.h>
#include <semaphore.h>
#include "progtest_solver.h"
#include "sample_tester.h"
using namespace std;
#endif /* __PROGTEST__ */


class COptimizer;
//-------------------------------------------------------------------------------------------------------------------------------------------------------------
class CCompanyWrapper
{
  public:
  //move?? std::move(company)
    explicit CCompanyWrapper (ACompany company): m_Company (company) {}

    thread m_ThrReceive;
    thread m_ThrReturn;


    //for receive - return thread comm
    std::mutex m_Mtx;                    
    std::condition_variable m_CVEmpty;

    void receiveProblem (COptimizer & optimizer);
    void returnProblem ();

    void problemToSolver (COptimizer & optimizer, std::vector<APolygon>  & problem, std::string type);

    void notifyQueue () { unique_lock<mutex> lock ( m_Mtx ); m_CVEmpty.notify_all(); }

    void pushProblem (AProblemPack & item);
    AProblemPack popProblem ();
    bool problemIsSolved (AProblemPack frontProblemPack);
    
    //debug
    // int m_companyID;
    
  private:

    ACompany m_Company;
    std::deque<AProblemPack> m_ProblemPacks;

};
using ACompanyWrapper = shared_ptr<CCompanyWrapper>;


//-------------------------------------------------------------------------------------------------------------------------------------------------------------
class COptimizer
{
  public:

    COptimizer ()
    : m_SolverMin (createProgtestMinSolver()), m_SolverCnt (createProgtestCntSolver()), m_CompanyWithAllProblems (0), m_KillWorkerThreads ( false ) {}

    static bool                        usingProgtestSolver                     ( void )
    {
      return true;
    }
    static void                        checkAlgorithmMin                       ( APolygon                              p )
    {
      // dummy implementation if usingProgtestSolver() returns true
    }
    static void                        checkAlgorithmCnt                       ( APolygon                              p )
    {
      // dummy implementation if usingProgtestSolver() returns true
    }
    void                               start                                   ( int                                   threadCount );
    void                               stop                                    ( void );
    void                               addCompany                              ( ACompany                              company );
    void                               worker                                  ();
    bool                               getNewSolver                            (std::string type); 
    bool                               addSolverToQue                          (std::string type);
    AProgtestSolver                    getSolver                               (std::string type);

    bool                               checkCompaniesHaveAllProblems           ();
    AProgtestSolver                    takeFirstSolverFromQue                  (std::queue<AProgtestSolver> & m_FullSolvers);    
    void                               notifyCompanies                         ();

    //for receive and working thread (calculation)
    std::mutex m_MtxSolver;
    std::condition_variable m_CVWorker;

    
    //min solver
    AProgtestSolver m_SolverMin;
    queue<AProgtestSolver> m_FullSolversMin;

    //cnt solver
    AProgtestSolver m_SolverCnt;
    queue<AProgtestSolver> m_FullSolversCnt;


    atomic_size_t m_CompanyWithAllProblems;
    //can kill working thread at the end
    bool m_KillWorkerThreads;

    //debug
    // atomic_size_t counter = 0;
    // atomic_size_t Atom_companyID = 0;

  private:

    std::vector<ACompanyWrapper> m_Companies;
    std::vector<thread> m_Workers;
};
// TODO: COptimizer implementation goes here

void COptimizer::addCompany (ACompany company ){
  m_Companies.emplace_back (make_shared<CCompanyWrapper>(company));
}

void COptimizer::start (int threadCount){

  //threads for calculation
  for ( int i = 0; i < threadCount; i++ ){
    m_Workers.emplace_back (&COptimizer::worker, this);
  }    
  
  //comm threads (receive and return problem) for every company
  for (auto & company : m_Companies){
    //need .get!!! To have instance | paramater to function must be like a reference 
    company->m_ThrReceive = std::thread(& CCompanyWrapper::receiveProblem, company.get(), std::ref(*this));
    company->m_ThrReturn = std::thread(& CCompanyWrapper::returnProblem, company.get());
  }          
}

void COptimizer::stop () {
//    fprintf ( stderr, "COptimizer::stop\n");
  for ( auto & worker : m_Workers )
    worker.join();
      
  for ( auto & company : m_Companies ){
    company->m_ThrReceive.join();
    company->m_ThrReturn.join();
  }
}
//make a calculation
void COptimizer::worker () {

  //  atomic_int id = counter++; 
  //  fprintf ( stderr, "WORKER: Starting %d\n", id.load() );

    // while loop will keep running until termination condition is met
    while ( true ) {

        unique_lock<mutex> lock(m_MtxSolver);

        m_CVWorker.wait (lock, [ this ] { return ! m_FullSolversMin.empty() || ! m_FullSolversCnt.empty() || m_KillWorkerThreads ;} );

        //cancel the worker
        if ( m_FullSolversMin.empty() && m_FullSolversCnt.empty() && m_KillWorkerThreads )
            break;

        bool min = false;
        bool cnt = false;
        AProgtestSolver solverMin;
        AProgtestSolver solverCnt;
        //take solver from que
        if (! m_FullSolversMin.empty()){         
          solverMin = takeFirstSolverFromQue(m_FullSolversMin);
          min = true;
        }
        if (! m_FullSolversCnt.empty()){
          solverCnt = takeFirstSolverFromQue(m_FullSolversCnt);
          cnt = true;
        }  
        lock.unlock();

        // Calculate for solverMin if available
        if (min){
          solverMin->solve();
        }
        // Calculate for solverCnt if available
        if (cnt){
          solverCnt->solve();
        }
        
        // fprintf ( stderr, "WORKER: Notifying companies %d\n", id.load() );
        notifyCompanies();
    }

//    fprintf ( stderr, "WORKER: Stopping %d\n", id.load() );
}

bool COptimizer::getNewSolver (std::string type) {

    if (type == "Min"){
      m_SolverMin = createProgtestMinSolver();
      return m_SolverMin != nullptr;
    }
    else if (type == "Cnt"){
      m_SolverCnt = createProgtestCntSolver();
      return m_SolverCnt != nullptr;
    }
    return false;
    
}

bool COptimizer::addSolverToQue (std::string type) {
    if (type == "Min"){
      
      m_FullSolversMin.push ( m_SolverMin );
      return true;
    }
    else if (type == "Cnt"){
      m_FullSolversCnt.push ( m_SolverCnt );
      return true;
    }
    return false;    
}

bool COptimizer::checkCompaniesHaveAllProblems (){
  return m_CompanyWithAllProblems.load() == m_Companies.size(); 
}

void COptimizer::notifyCompanies (){

  for (auto & company : m_Companies){
    company->notifyQueue();
  }  
}

AProgtestSolver COptimizer::getSolver (std::string type) {
  if (type == "Min"){
    return m_SolverMin;
  }
  else if (type == "Cnt"){
    return m_SolverCnt;
  }
  return nullptr;    
}

AProgtestSolver COptimizer::takeFirstSolverFromQue (std::queue<AProgtestSolver> & m_FullSolvers){

  if (!m_FullSolvers.empty()){
    auto solver = m_FullSolvers.front();
    m_FullSolvers.pop();
    return solver;
  }
  
  return nullptr;
}


void CCompanyWrapper::problemToSolver(COptimizer & optimizer, vector<APolygon>  & problems, string type){

  for (auto & problem : problems) {
    
    optimizer.getSolver(type)->addPolygon (problem);

    //check if solver full
    if (!optimizer.getSolver(type)->hasFreeCapacity()) {
      optimizer.addSolverToQue(type);
      optimizer.getNewSolver(type);
    }
  }

}

void CCompanyWrapper::receiveProblem (COptimizer & optimizer){

    //  fprintf ( stderr, "RECEIVER: start %d\n", m_CompanyID);
    //  fprintf ( stderr, "RECEIVER: start %d\n", 0);
  while (AProblemPack problemPack = m_Company->waitForPack()) {

    //push problem inside of company 
    pushProblem(problemPack);

    //adding problems to solver
    unique_lock<mutex> lock (optimizer.m_MtxSolver);

    //I can create solverWrapper and write there which companies have packs there. Then will be a little faster notifyCompanies in worker. 

    problemToSolver(optimizer, problemPack->m_ProblemsMin, "Min");
    problemToSolver(optimizer, problemPack->m_ProblemsCnt, "Cnt");

    lock.unlock(); 
    optimizer.m_CVWorker.notify_one(); 
  }
  
  //signal that no next new problem will be added 
  AProblemPack lastPack = nullptr;
  pushProblem(lastPack);

  //no more problems to add to solver
  optimizer.m_CompanyWithAllProblems++;

  //check if the last company
  unique_lock<mutex> lock ( optimizer.m_MtxSolver );
  if ( optimizer.checkCompaniesHaveAllProblems() ) {

    //add solvers which are not full
    optimizer.addSolverToQue("Min");  
    optimizer.addSolverToQue("Cnt");

    optimizer.m_KillWorkerThreads = true;
    optimizer.m_CVWorker.notify_all();
  }
  //  fprintf ( stderr, "RECEIVER: stop %d\n", m_CompanyID );
  // fprintf ( stderr, "RECEIVER: stop %d\n", 0);
}

void CCompanyWrapper::returnProblem () {
//    fprintf ( stderr, "RETURNER: start%d\n", m_CompanyID );
    while ( auto pack = popProblem() ) {
//        fprintf ( stderr, "RETURNER: returning pack %d\n", m_CompanyID );
        m_Company->solvedPack (pack);
    }
//    fprintf ( stderr, "RETURNER: stop %d\n", m_CompanyID);
}

void CCompanyWrapper::pushProblem (AProblemPack & item) {
      
      // access to the shared queue
      unique_lock<mutex> lock ( m_Mtx );

      m_ProblemPacks.push_back (item);
      m_CVEmpty.notify_all();
}

AProblemPack CCompanyWrapper::popProblem () {

  unique_lock<mutex> lock ( m_Mtx );

  //waiting until one of the following conditions is met:
  m_CVEmpty.wait (lock, [ this ] {
      // last pack  is nullptr at front
      return ( ! m_ProblemPacks.empty() && m_ProblemPacks.front() == nullptr ) 
          || ( ! m_ProblemPacks.empty() && problemIsSolved(m_ProblemPacks.front())); } );

  AProblemPack item  = m_ProblemPacks.front();
  m_ProblemPacks.pop_front();
  return item;
} 

bool CCompanyWrapper::problemIsSolved (AProblemPack frontProblemPack){

  for (auto & problemMin : frontProblemPack->m_ProblemsMin)
        if (problemMin->m_TriangMin == 0)
            return false;
  for (auto & problemCnt : frontProblemPack->m_ProblemsCnt)
        if (problemCnt->m_TriangCnt == 0)
            return false;          
  return true;
}     

//-------------------------------------------------------------------------------------------------------------------------------------------------------------
#ifndef __PROGTEST__
int                                    main                                    ( void )
{
  COptimizer optimizer;
  ACompanyTest  company = std::make_shared<CCompanyTest> ();
  optimizer . addCompany ( company );
  optimizer . start ( 4 );
  optimizer . stop  ();
  if ( ! company -> allProcessed () )
    throw std::logic_error ( "(some) problems were not correctly processsed" );
  return 0;
}
#endif /* __PROGTEST__ */

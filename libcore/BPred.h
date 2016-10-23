/*
   SESC: Super ESCalar simulator
   Copyright (C) 2003 University of Illinois.

   Contributed by Jose Renau
                  Smruti Sarangi
                  Milos Prvulovic

This file is part of SESC.

SESC is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation;
either version 2, or (at your option) any later version.

SESC is    distributed in the  hope that  it will  be  useful, but  WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should  have received a copy of  the GNU General  Public License along with
SESC; see the file COPYING.  If not, write to the  Free Software Foundation, 59
Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#ifndef BPRED_H
#define BPRED_H

/*
 * The original Branch predictor code was inspired in simplescalar-3.0, but
 * heavily modified to make it more OOO. Now, it supports more branch predictors
 * that the standard simplescalar distribution.
 *
 * Supported branch predictors models:
 *
 * Oracle, NotTaken, Taken, 2bit, 2Level, 2BCgSkew
 *
 */

#include "nanassert.h"
#include "estl.h"

#include "GStats.h"
#include "libll/Instruction.h"        // InstID
#include "CacheCore.h"
#include "EnergyMgr.h"
#include "SCTable.h"

#define RAP_T_NT_ONLY   1

enum PredType {
    CorrectPrediction = 0,
    NoPrediction,
    NoBTBPrediction,
    MissPrediction
};

class BPred {
public:
    typedef unsigned long long HistoryType;
    class Hash4HistoryType {
    public:
        size_t operator()(const HistoryType &addr) const {
            return ((addr) ^ (addr>>16));
        }
    };

protected:
    const int32_t id;

    GStatsCntr nHit;  // N.B. predictors should not update these counters directly
    GStatsCntr nMiss; // in their predict() function.

    GStatsEnergy *bpredEnergy;
    int32_t bpred4Cycle;
    int32_t bpred4CycleAddrShift;


protected:
public:
    BPred(int32_t i, int32_t fetchWidth, const char *section, const char *name);
    virtual ~BPred();

    HistoryType calcInstID(const Instruction *inst) const {
        HistoryType cid = inst->currentID(); // psudo-PC works, no need addr (slower)

        // Remove used bits (restrict predictions per cycle)
        cid = cid>>bpred4CycleAddrShift;
        // randomize it
        cid = (cid >> 17) ^ (cid);

        return cid;
    }


    // If oracleID is not passed, the predictor is not updaed
    virtual PredType predict(const Instruction *inst, InstID oracleID, bool doUpdate) = 0;

    PredType doPredict(const Instruction *inst, InstID oracleID, bool doUpdate) {
        PredType pred = predict(inst, oracleID, doUpdate);
        if (!doUpdate || pred == NoPrediction)
            return pred;

        nHit.cinc(pred == CorrectPrediction);
        nMiss.cinc(pred != CorrectPrediction);

        return pred;
    }

    virtual void switchIn(Pid_t pid)  = 0;
    virtual void switchOut(Pid_t pid) = 0;
};


class BPRas : public BPred {
private:
    const ushort RasSize;

    InstID *stack;
    int32_t index;
    GStatsEnergy *rasEnergy;
protected:
public:
    BPRas(int32_t i, int32_t fetchWidth, const char *section);
    ~BPRas();
    PredType predict(const Instruction * inst, InstID oracleID, bool doUpdate);

    void switchIn(Pid_t pid);
    void switchOut(Pid_t pid);
};

class BPBTB : public BPred {
private:
    GStatsEnergy *btbEnergy;

    class BTBState : public StateGeneric<> {
    public:
        BTBState() {
            inst = 0;
        }

        InstID inst;

        bool operator==(BTBState s) const {
            return inst == s.inst;
        }
    };

    typedef CacheGeneric<BTBState, uint32_t, false> BTBCache;

    BTBCache *data;

protected:
public:
    BPBTB(int32_t i, int32_t fetchWidth, const char *section, const char *name=0);
    ~BPBTB();

    PredType predict(const Instruction * inst, InstID oracleID, bool doUpdate);
    void updateOnly(const Instruction * inst, InstID oracleID);

    void switchIn(Pid_t pid);
    void switchOut(Pid_t pid);
};

class BPOracle : public BPred {
private:
    BPBTB btb;

protected:
public:
    BPOracle(int32_t i, int32_t fetchWidth, const char *section)
        :BPred(i, fetchWidth, section, "Oracle")
        ,btb(i, fetchWidth, section) {
    }

    PredType predict(const Instruction * inst, InstID oracleID, bool doUpdate);

    void switchIn(Pid_t pid);
    void switchOut(Pid_t pid);
};

class BPNotTaken : public BPred {
private:
protected:
public:
    BPNotTaken(int32_t i, int32_t fetchWidth, const char *section)
        :BPred(i, fetchWidth, section, "NotTaken") {
        // Done
    }

    PredType predict(const Instruction * inst, InstID oracleID, bool doUpdate);

    void switchIn(Pid_t pid);
    void switchOut(Pid_t pid);
};

class BPTaken : public BPred {
private:
    BPBTB btb;

protected:
public:
    BPTaken(int32_t i, int32_t fetchWidth, const char *section)
        :BPred(i, fetchWidth, section, "Taken")
        ,btb(  i, fetchWidth, section) {
        // Done
    }

    PredType predict(const Instruction * inst, InstID oracleID, bool doUpdate);

    void switchIn(Pid_t pid);
    void switchOut(Pid_t pid);
};

class BPStatic:public BPred {
private:
    BPBTB btb;

protected:
public:
    BPStatic(int32_t i, int32_t fetchWidth, const char *section)
        :BPred(i, fetchWidth, section, "Static")
        ,btb(  i, fetchWidth, section) {
        // Done
    }

    PredType predict(const Instruction * inst, InstID oracleID, bool doUpdate);

    void switchIn(Pid_t pid);
    void switchOut(Pid_t pid);
};

class BP2bit:public BPred {
private:
    BPBTB btb;

    SCTable table;
protected:
public:
    BP2bit(int32_t i, int32_t fetchWidth, const char *section);

    PredType predict(const Instruction * inst, InstID oracleID, bool doUpdate);

    void switchIn(Pid_t pid);
    void switchOut(Pid_t pid);
};

class BP2level:public BPred {
private:
    BPBTB btb;

    const ushort l1Size;
    const uint32_t  l1SizeMask;

    const ushort historySize;
    const HistoryType historyMask;

    SCTable globalTable;

    HistoryType *historyTable; // LHR
protected:
public:
    BP2level(int32_t i, int32_t fetchWidth, const char *section);
    ~BP2level();

    PredType predict(const Instruction * inst, InstID oracleID, bool doUpdate);

    void switchIn(Pid_t pid);
    void switchOut(Pid_t pid);
};

class BPHybrid:public BPred {
private:
    BPBTB btb;

    const ushort historySize;
    const HistoryType historyMask;

    SCTable globalTable;

    HistoryType ghr; // Global History Register

    SCTable localTable;
    SCTable metaTable;

protected:
public:
    BPHybrid(int32_t i, int32_t fetchWidth, const char *section);
    ~BPHybrid();

    PredType predict(const Instruction * inst, InstID oracleID, bool doUpdate);

    void switchIn(Pid_t pid);
    void switchOut(Pid_t pid);
};

class BP2BcgSkew : public BPred {
private:
    BPBTB btb;

    SCTable BIM;

    SCTable G0;
    const ushort       G0HistorySize;
    const HistoryType  G0HistoryMask;

    SCTable G1;
    const ushort       G1HistorySize;
    const HistoryType  G1HistoryMask;

    SCTable metaTable;
    const ushort       MetaHistorySize;
    const HistoryType  MetaHistoryMask;

    HistoryType history;
protected:
public:
    BP2BcgSkew(int32_t i, int32_t fetchWidth, const char *section);
    ~BP2BcgSkew();

    PredType predict(const Instruction * inst, InstID oracleID, bool doUpdate);

    void switchIn(Pid_t pid);
    void switchOut(Pid_t pid);
};

class BPyags : public BPred {
private:
    BPBTB btb;

    const ushort historySize;
    const HistoryType historyMask;

    SCTable table;
    SCTable ctableTaken;
    SCTable ctableNotTaken;

    HistoryType ghr; // Global History Register

    uchar *CacheTaken;
    HistoryType CacheTakenMask;
    HistoryType CacheTakenTagMask;

    uchar *CacheNotTaken;
    HistoryType CacheNotTakenMask;
    HistoryType CacheNotTakenTagMask;

protected:
public:
    BPyags(int32_t i, int32_t fetchWidth, const char *section);
    ~BPyags();

    PredType predict(const Instruction * inst, InstID oracleID, bool doUpdate);

    void switchIn(Pid_t pid);
    void switchOut(Pid_t pid);
};

class BPOgehl : public BPred {
private:
    BPBTB btb;

    const int32_t M_SIZ;
    const int32_t glength;
    const int32_t nentry;
    const int32_t addwidth;
    int32_t logpred;

    int32_t THETA;
    int32_t MAXTHETA;
    int32_t THETAUP;
    int32_t PREDUP;

    long long phist;
    long long *ghist;
    int32_t *histLength;
    int32_t *usedHistLength;

    int32_t *T;
    int32_t AC;
    int32_t miniTag;
    char *MINITAG;

    char **pred;
    int32_t TC;
protected:
    int32_t geoidx(long long Add, long long *histo, long long phisto, int32_t m, int32_t funct);
public:
    BPOgehl(int32_t i, int32_t fetchWidth, const char *section);
    ~BPOgehl();

    PredType predict(const Instruction * inst, InstID oracleID, bool doUpdate);

    void switchIn(Pid_t pid);
    void switchOut(Pid_t pid);
};

struct BranchRecord {
    int correctPredictions;
    int mispredictions;
};

class BPredictor {
private:
    const int32_t id;
    const bool SMTcopy;

    BPRas ras;
    BPred *pred;

    GStatsCntr nBranches;
    GStatsCntr nTaken;
    GStatsCntr nMiss;           // hits == nBranches - nMiss

    char *section;

typedef unsigned long long HistoryType;

    std::unordered_map<HistoryType, BranchRecord> predictionRecords;


protected:
public:
    BPredictor(int32_t i, int32_t fetchWidth, const char *section, BPredictor *bpred=0);
    ~BPredictor();

    static BPred *getBPred(int32_t id, int32_t fetchWidth, const char *sec);

    void recordPredictionResult(const Instruction *instruction, PredType prediction) {
         HistoryType instructionId = pred->calcInstID(instruction);

         if (predictionRecords.find(instructionId) == predictionRecords.end()) {
             predictionRecords[instructionId] = { 0, 0 };
         } 

         if (prediction == CorrectPrediction) {
             predictionRecords[instructionId].correctPredictions += 1;
         } else {
             predictionRecords[instructionId].mispredictions += 1;
         }
    }

    PredType predict(const Instruction *inst, InstID oracleID, bool doUpdate) {
        I(inst->isBranch());

        nBranches.cinc(doUpdate);
        nTaken.cinc(inst->calcNextInstID() != oracleID);

        PredType p= ras.doPredict(inst, oracleID, doUpdate);
        if( p != NoPrediction ) {
            nMiss.cinc(p != CorrectPrediction && doUpdate);
            return p;
        }

        p = pred->doPredict(inst, oracleID, doUpdate);

        if (inst->isBranch()) {
            recordPredictionResult(inst, p);
        }

        nMiss.cinc(p != CorrectPrediction && doUpdate);

        return p;
    }

    void dump(const char *str) const;

    void switchIn(Pid_t pid) {
        pred->switchIn(pid);
    }

    void switchOut(Pid_t pid) {
        pred->switchOut(pid);
        printResults();
    }

    void printResults() {
        int numInstructionsExecutedLessThan10Times = 0;
        int numInstructionsExecuted10To99Times = 0;
        int numInstructionsExecuted100To999Times = 0;
        int numInstructionsExecutedMoreThan999Times = 0;

        int numCorrectPredictionsLessThan10Times = 0;
        int numMispredictionsLessThan10Times = 0;
        int numCorrectPredictions10To99Times = 0;
        int numMispredictions10To99Times = 0;
        int numCorrectPredictions100To999Times = 0;
        int numMispredictions100To999Times = 0;
        int numCorrectPredictionsMoreThan999Times = 0;
        int numMispredictionsMoreThan999Times = 0;

        for (auto kv : predictionRecords) {
            BranchRecord record = kv.second;

            int totalExecutions = record.correctPredictions + record.mispredictions;

            if (totalExecutions < 10) {
                numInstructionsExecutedLessThan10Times += 1;
                numCorrectPredictionsLessThan10Times += record.correctPredictions;
                numMispredictionsLessThan10Times += record.mispredictions;
            } else if (totalExecutions >= 10 && totalExecutions < 100) {
                numInstructionsExecuted10To99Times += 1;
                numCorrectPredictions10To99Times += record.correctPredictions;
                numMispredictions10To99Times += record.mispredictions;
            } else if (totalExecutions >= 100 && totalExecutions < 1000) {
                numInstructionsExecuted100To999Times += 1;
                numCorrectPredictions100To999Times += record.correctPredictions;
                numMispredictions100To999Times += record.mispredictions;
            } else if (totalExecutions >= 1000) {
                numInstructionsExecutedMoreThan999Times += 1;
                numCorrectPredictionsMoreThan999Times += record.correctPredictions;
                numMispredictionsMoreThan999Times += record.mispredictions;
            }
        }
        printf("Instructions Executed \n1-9 Times:\t%d \n10-99 Times:\t%d \n100-999 Times:\t%d \n1000+ Times:\t%d\n\n", numInstructionsExecutedLessThan10Times, numInstructionsExecuted10To99Times, numInstructionsExecuted100To999Times, numInstructionsExecutedMoreThan999Times);
        
        float accuracyInstructionsExecutedLessThan10Times = ((float) numCorrectPredictionsLessThan10Times) / ((float) (numCorrectPredictionsLessThan10Times + numMispredictionsLessThan10Times));
        float accuracyInstructionsExecuted10To99Times = ((float) numCorrectPredictions10To99Times) / ((float) (numCorrectPredictions10To99Times + numMispredictions10To99Times));
        float accuracyInstructionsExecuted100To999Times = ((float) numCorrectPredictions100To999Times) / ((float) (numCorrectPredictions100To999Times + numMispredictions100To999Times));
        float accuracyInstructionsExecutedMoreThan999Times = ((float) numCorrectPredictionsMoreThan999Times) / ((float) (numCorrectPredictionsMoreThan999Times + numMispredictionsMoreThan999Times));
        
        printf("Accuracies for instructions executed \n1-9Times:\t%f \n10-99 Times:\t%f \n100-999 Times:\t%f \n 1000+ Times:\t%f\n\n", accuracyInstructionsExecutedLessThan10Times, accuracyInstructionsExecuted10To99Times, accuracyInstructionsExecuted100To999Times, accuracyInstructionsExecutedMoreThan999Times);
    }
};

#endif   // BPRED_H
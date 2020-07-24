/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2017, The Souffle Developers. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file MagicSet.h
 *
 * Define classes and functionality related to the magic set transformation.
 *
 ***********************************************************************/

#pragma once

#include "ast/AstAbstract.h"
#include "ast/AstArgument.h"
#include "ast/AstClause.h"
#include "ast/AstLiteral.h"
#include "ast/AstQualifiedName.h"
#include "ast/AstVisitor.h"
#include "ast/transform/AstTransformer.h"
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace souffle {

class AstTranslationUnit;

/**
 * Database normaliser for MST.
 * Effects:
 *  - Partitions database into [input|intermediate|queries]
 *  - Normalises all arguments and constraints
 * Prerequisite for adornment.
 */
class NormaliseDatabaseTransformer : public AstTransformer {
public:
    std::string getName() const override {
        return "NormaliseDatabaseTransformer";
    }

    NormaliseDatabaseTransformer* clone() const override {
        return new NormaliseDatabaseTransformer();
    }

private:
    bool transform(AstTranslationUnit& translationUnit) override;

    /**
     * Partitions the input and output relations.
     * Program will no longer have relations that are both input and output.
     */
    static bool partitionIO(AstTranslationUnit& translationUnit);

    /**
     * Separates the IDB from the EDB, so that they are disjoint.
     * Program will no longer have input relations that appear as the head of clauses.
     */
    static bool extractIDB(AstTranslationUnit& translationUnit);

    /**
     * Extracts output relations into separate simple query relations,
     * so that they are unused in any other rules.
     * Programs will only contain output relations which:
     *      (1) have exactly one rule defining them
     *      (2) do not appear in other rules
     */
    static bool querifyOutputRelations(AstTranslationUnit& translationUnit);

    /**
     * Normalise all arguments within each clause.
     * All arguments in all clauses will now be either:
     *      (1) a variable, or
     *      (2) the RHS of a `<var> = <arg>` constraint
     */
    static bool normaliseArguments(AstTranslationUnit& translationUnit);
};

/**
 * Database labeller. Runs the magic-set labelling algorithm.
 * Necessary for supporting negation in MST.
 */
class LabelDatabaseTransformer : public AstTransformer {
public:
    std::string getName() const override {
        return "LabelDatabaseTransformer";
    }

    LabelDatabaseTransformer* clone() const override {
        return new LabelDatabaseTransformer();
    }

private:
    bool transform(AstTranslationUnit& translationUnit) override;

    /**
     * Runs the first stage of the labelling algorithm.
     * Separates out negated appearances of relations from the main SCC graph, preventing
     * them from affecting stratification once magic dependencies are added.
     */
    static bool runNegativeLabelling(AstTranslationUnit& translationUnit);

    /**
     * Runs the second stage of the labelling algorithm.
     * Separates out the dependencies of negatively labelled atoms from the main SCC
     * graph, preventing them from affecting stratification after magic.
     * Negative labelling must have been run first.
     */
    static bool runPositiveLabelling(AstTranslationUnit& translationUnit);

    /**
     * Provide a unique name for negatively-labelled relations.
     */
    static AstQualifiedName getNegativeLabel(const AstQualifiedName& name);

    /**
     * Provide a unique name for a positively labelled relation copy.
     */
    static AstQualifiedName getPositiveLabel(const AstQualifiedName& name, size_t count);

    /**
     * Check if a relation is negatively labelled.
     */
    static bool isNegativelyLabelled(const AstQualifiedName& name);
};

/**
 * Database adornment.
 * Adorns the rules of a database with variable flow and binding information.
 * Prerequisite for the magic set transformation.
 */
class AdornDatabaseTransformer : public AstTransformer {
public:
    std::string getName() const override {
        return "AdornDatabaseTransformer";
    }

    AdornDatabaseTransformer* clone() const override {
        return new AdornDatabaseTransformer();
    }

private:
    using adorned_predicate = std::pair<AstQualifiedName, std::string>;

    std::set<adorned_predicate> headAdornmentsToDo;
    std::set<AstQualifiedName> headAdornmentsSeen;

    std::vector<std::unique_ptr<AstClause>> adornedClauses;
    std::vector<std::unique_ptr<AstClause>> redundantClauses;
    std::set<AstQualifiedName> relationsToIgnore;

    bool transform(AstTranslationUnit& translationUnit) override;

    /**
     * Get the unique identifier corresponding to an adorned predicate.
     */
    static AstQualifiedName getAdornmentID(
            const AstQualifiedName& relName, const std::string& adornmentMarker);

    /**
     * Add an adornment to the ToDo queue if it hasn't been processed before.
     */
    void queueAdornment(const AstQualifiedName& relName, const std::string& adornmentMarker) {
        auto adornmentID = getAdornmentID(relName, adornmentMarker);
        if (!contains(headAdornmentsSeen, adornmentID)) {
            headAdornmentsToDo.insert(std::make_pair(relName, adornmentMarker));
            headAdornmentsSeen.insert(adornmentID);
        }
    }

    /**
     * Check if any more relations need to be adorned.
     */
    bool hasAdornmentToProcess() const {
        return !headAdornmentsToDo.empty();
    }

    /**
     * Pop off the next predicate adornment to process.
     */
    adorned_predicate nextAdornmentToProcess() {
        assert(hasAdornmentToProcess() && "no adornment to pop");
        auto headAdornment = *(headAdornmentsToDo.begin());
        headAdornmentsToDo.erase(headAdornmentsToDo.begin());
        return headAdornment;
    }

    /**
     * Returns the adorned version of a clause.
     */
    std::unique_ptr<AstClause> adornClause(const AstClause* clause, const std::string& adornmentMarker);
};

/**
 * Magic Set Transformation.
 * Before running this transformation, need to run:
 *      (1) NormaliseDatabaseTransformer, for assumptions to hold
 *      (2) LabelDatabaseTransformer, to support negation
 *      (3) AdornDatabaseTransformer, to annotate information flow
 */
class MagicSetTransformer : public AstTransformer {
public:
    std::string getName() const override {
        return "MagicSetTransformer";
    }

    MagicSetTransformer* clone() const override {
        return new MagicSetTransformer();
    }

private:
    bool transform(AstTranslationUnit& translationUnit) override;
};

/**
 * A storage of bound variables that dynamically determines the set of bound variables
 * within a clause.
 */
class BindingStore {
public:
    BindingStore(const AstClause* clause, const std::string& adornmentMarker);

    void bindVariable(std::string varName) {
        boundVariables.insert(varName);
        reduceDependencies();
    }

    bool isBound(std::string varName) const {
        return contains(boundVariables, varName) || contains(boundHeadVariables, varName);
    }

    const std::set<std::string>& getBoundVariables() const {
        return boundVariables;
    }

private:
    // Helper types to represent a disjunction of several dependency sets
    using ConjBindingSet = std::set<std::string>;
    using DisjBindingSet = std::set<ConjBindingSet>;

    std::set<std::string> boundVariables{};
    std::set<std::string> boundHeadVariables{};
    std::map<std::string, DisjBindingSet> variableDependencies{};

    /**
     * Add a new conjunction of variables as a potential binder for a given variable.
     * The variable is considered bound if all variables in the conjunction are bound.
     */
    void addBindingDependency(std::string variable, ConjBindingSet dependency) {
        if (!contains(variableDependencies, variable)) {
            variableDependencies[variable] = DisjBindingSet();
        }
        variableDependencies[variable].insert(dependency);
    }

    /**
     * Add binding dependencies formed on lhs by a <lhs> = <rhs> equality constraint.
     */
    void processEqualityBindings(const AstArgument* lhs, const AstArgument* rhs);

    /**
     * Generate all binding dependencies implied by the constraints within a given clause.
     */
    void generateBindingDependencies(const AstClause* clause);

    /**
     * Reduce a conjunctive set of dependencies based on the current bound variable set.
     */
    ConjBindingSet reduceDependency(const ConjBindingSet& origDependency);

    /**
     * Reduce a disjunctive set of variable dependencies based on the current bound variable set.
     */
    DisjBindingSet reduceDependency(const DisjBindingSet& origDependency);

    /**
     * Reduce the full set of dependencies for all tracked variables, binding whatever needs to be bound.
     */
    bool reduceDependencies();
};

}  // namespace souffle

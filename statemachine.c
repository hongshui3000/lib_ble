/****************************************************************************
 *
 * File:
 *     $Id: statemachine.c 11179 2014-07-10 00:40:10Z khe $
 *     $Product: OpenSynergy Blue SDK v4.x $
 *     $Revision: 11179 $
 *
 * Description: Implementation of a generic State Machine 
 *
 * Copyright 2010-2014 OpenSynergy GmbH
 * All rights reserved. All unpublished rights reserved.
 *
 * Unpublished Confidential Information of OpenSynergy GmbH.  
 * Do Not Disclose.
 *
 * No part of this work may be used or reproduced in any form or by any 
 * means, or stored in a database or retrieval system, without prior written 
 * permission of OpenSynergy GmbH.
 * 
 * Use of this work is governed by a license granted by OpenSynergy GmbH. 
 * This work contains confidential and proprietary information of 
 * OpenSynergy GmbH which is protected by copyright, trade secret, 
 * trademark and other intellectual property rights.
 *
 ****************************************************************************/
#include "statemachine.h"

/****************************************************************************
 *
 * Prototypes
 *
 ****************************************************************************/

#define Report(fmt, ...)    custom_log("", fmt, ##__VA_ARGS__)
#define XAD_Decode(state)   #state

static void smInsertRule(StateMachine *sm, SmRule *rule);

/****************************************************************************
 *
 * Local configuration
 *
 ****************************************************************************/

/**
 * The maximum number of items in a superstate hierarchy chain which all 
 * have Enter- rules. It is very unlikely to need more than this.
 */
#ifndef SM_MAX_CHAIN_DEPTH
#define SM_MAX_CHAIN_DEPTH 12
#endif

/****************************************************************************
 *
 * Module-specific functions
 *
 ****************************************************************************/

/**
 * Hunts through rules for the first rule corresponding to 
 * a given state.
 */
static SmRule *smLookupState(StateMachine *sm, uint8_t state)
{
    uint32_t pos;

    for(pos = 0; pos < sm->ruleCount; pos++) {
        if (sm->rules[pos].state == state) return &sm->rules[pos];
        if (sm->rules[pos].nextStatePos) {
            pos = sm->rules[pos].nextStatePos - 1;
        }
    }
    return 0;
}

/**
 * Hunts for a rule that matches the state in the specified start-rule.
 */
static const SmRule *smFindEventRule(StateMachine *sm, SmRule *state, uint32_t eventType)
{
    uint16_t pos; 

    /* Dummy states have no event rules */
    if ((state == &sm->tempState) || (state == &sm->tempState2)) return 0;

    while(state) {
        SmRule *cur = state;

        /* Iterate from the state upwards through its inheritance chain */
        for (pos = state - sm->rules; pos < sm->ruleCount; cur = &sm->rules[++pos]) {

            /* Have we moved past the requested state? */
            if (cur->state != state->state) break;

            /* Skip forward to event-based rules for this state*/
            if (cur->type != SMR_EVENT) continue;

            /* Have we moved past the requested event type? */
            if (cur->u.evt.eventType > eventType) break;
            
            if (cur->u.evt.eventType == eventType) {
                /* Event-type match */
                if (cur->u.evt.action == 0 && cur->u.evt.nextState == state->state) {
                    /* This a "block"; return as if no rule was found */
                    return 0;
                }
                return cur;
            }  
        }

        /* If this state has a superstate then transition to it and look again. */
        if (state->type == SMR_INHERIT) {
            state = state->u.inherit.superStateRule;
        } else {
            state = 0;
        }
    }
    return 0;
}


/**
 * Transitions from oldState to newState, firing any appropriate
 * entry or exit rules along the way.
 */
static void smTransition(StateMachine *sm, SmRule *oldState, SmRule *newState)
{    
    SmRule *topState, *curState;
    uint16_t superChain[SM_MAX_CHAIN_DEPTH];
    uint32_t superChainPos = 0;

    ASSERT(oldState && newState);

    /* No transition to make */
    if (oldState == newState) return;

    /* Locate the first superstate shared by both old and new. */
    for(topState = oldState; topState->type == SMR_INHERIT;
        topState = topState->u.inherit.superStateRule) {

        /* For each step in the old state's inheritance chain, walk up the 
         * full inheritance chain of the new state */
        for(curState = newState; curState->type == SMR_INHERIT;
            curState = curState->u.inherit.superStateRule) {
            if (curState == topState) break;
        }

        /* If a match was found, stop looking */
        if (curState == topState) break;
    }


    /* Invoke exit rules for states we are exiting, bottom to top */
    curState = oldState;

    do {
        uint16_t pos;
    
        /* Dummy states have no exit rules. */
        if ((curState == &sm->tempState) || (curState == &sm->tempState2)) break;

        /* Scan forward for an exit rule */
        for(pos = curState - sm->rules;
            pos < sm->ruleCount && sm->rules[pos].state == curState->state;
            pos++) {
            if (sm->rules[pos].type == SMR_EXIT) {
#if XA_DECODER == MICO_TRUE
                if (sm->decode) {
                    Report("%s(%lx): Exiting %s, calling %s\n", sm->prefix, (uint32_t)sm->context,
                           XAD_Decode(oldState->state),
                           sm->rules[pos].u.enterExit.actionName);
                }
#endif /* XA_DECODER == MICO_TRUE */

                sm->rules[pos].u.enterExit.action(sm->context);
                break;

            } else if (sm->rules[pos].type > SMR_EXIT) break;
        }

        /* Break if no further superstates, or proceed */
        if (curState->type != SMR_INHERIT) break;
        curState = curState->u.inherit.superStateRule;
    } while(curState != topState);


    /* Exits are done, state is now changed. */
    sm->state = newState;
    sm->lastState = oldState;

    /* If this was a transition from tempState2, store the current state
     * in tempState so that tempState2 remains free in the future.
     */
    if (sm->state == &sm->tempState2) {
        SmRule hold = sm->tempState;
        sm->tempState = sm->tempState2;
        sm->state = &sm->tempState;

        if (sm->lastState == &sm->tempState) {
            sm->tempState2 = hold;
            sm->lastState = &sm->tempState2;
        }
    }

    /* Compile a list of superstates with enter rules */
    curState = newState;
    do {
        uint16_t pos;

        /* Dummy states have no enter rules. */
        if ((curState == &sm->tempState) || (curState == &sm->tempState2)) break;

        for(pos = curState - sm->rules;
            pos < sm->ruleCount && sm->rules[pos].state == curState->state;
            pos++) {

            /* IF we're beyond the enter type, stop looking for this state */
            if (sm->rules[pos].type > SMR_ENTER) break;
                
            if (sm->rules[pos].type == SMR_ENTER) {

                /* Highly unlikely that there will be a chain this long;
                 * if so; SM_MAX_CHAN_DEPTH will need to be increased.
                 */
                ASSERT(superChainPos < SM_MAX_CHAIN_DEPTH);

                /* Found an enter-rule, store its location */
                superChain[superChainPos++] = pos;
                break;
            }

        }
        /* Break if no further superstates, or proceed */
        if (curState->type != SMR_INHERIT) break;
        curState = curState->u.inherit.superStateRule;
    } while(curState != topState);

    /* Invoke entry rules for states we are entering, top to bottom */
    while(superChainPos > 0) {

#if XA_DECODER == MICO_TRUE
        if (sm->decode) {
            Report("%s(%lx): Entering %s, calling %s\n", sm->prefix, (uint32_t)sm->context,
                   XAD_Decode(newState->state),
                   sm->rules[superChain[superChainPos - 1]].u.enterExit.actionName);
        }
#endif /* XA_DECODER == MICO_TRUE */

        /* Launch the enter rule as we go back down the chain */
        (sm->rules[superChain[--superChainPos]].u.enterExit.action)(sm->context);
    }
}

/* Insertion-sorts a rule into the rule table */
static void smInsertRule(StateMachine *sm, SmRule *rule)
{
    uint16_t pos, copyPos;

    /* Must have room */
    ASSERT(sm->ruleCount < sm->ruleMax); 
    ASSERT(!(sm->flags & SMF_FINALIZED));

    /* We decide this, not the caller */
    rule->nextStatePos = 0;

    /* Search for the proper location for this new rule */
    for(pos = 0; pos < sm->ruleCount; pos++)
    {
        /* Sort on state */
        if (sm->rules[pos].state < rule->state) continue;

        /* If we go past state then this is the insertion point*/
        if (sm->rules[pos].state > rule->state) break;

        ASSERT(sm->rules[pos].state == rule->state);

        /* State matches, now sort by rule type */
        if (sm->rules[pos].type < rule->type) continue;
        if (sm->rules[pos].type > rule->type) break;

        /* Rule type matches (better be SMR_EVENT; otherwise this
         * is an illegal attempt to overwrite a matching rule.
         */
        ASSERT(sm->rules[pos].type == rule->type);
        ASSERT(rule->type == SMR_EVENT);

        /* State and type match, sort on event type */
        if (sm->rules[pos].u.evt.eventType < rule->u.evt.eventType) continue;

        /* Illegal to attempt to re-process the same event with a different action */
        ASSERT(sm->rules[pos].u.evt.eventType != rule->u.evt.eventType);
        break;
    }

    /* Shift rules down the array to make room for the new rule. */
    for(copyPos = sm->ruleCount; copyPos > pos; copyPos--) {
        sm->rules[copyPos] = sm->rules[copyPos-1];
    }
    sm->rules[copyPos] = *rule;   
    sm->ruleCount++;
}

/****************************************************************************
 *
 * Public functions
 *
 ****************************************************************************/

/**
 * Initializes a state machine for use.
 */
void SM_Init(StateMachine *sm, const SmInitParms *parms)
{
    ASSERT(sm && parms && parms->context);
    memset((uint8_t *)sm, 0, sizeof(*sm));
    sm->context = parms->context;
    sm->rules = parms->rules;
    sm->ruleMax = parms->maxRules;
    sm->ruleCount = 0;
    sm->lastState = 0;
    sm->initState = parms->initState;

#if XA_DECODER == MICO_TRUE
    // sm->stateTypeName = "";
    // sm->eventTypeName = "";
    sm->decode = FALSE;
#endif
}

void SM_Finalize(StateMachine *sm)
{
    SmRule *rule, *superRule;
    uint16_t pos, lastPos;
    uint8_t lastState;

    /* Only needs to be finalized once */
    ASSERT(!(sm->flags & SMF_FINALIZED));

    /* Go through the whole array and set nextEventPos locations */
    ASSERT(sm->ruleCount);
    lastState = sm->rules[0].state;
    lastPos = 0;
    for(pos = 0; pos < sm->ruleCount; pos++) {
        if (sm->rules[pos].state != lastState) {
            sm->rules[lastPos].nextStatePos = pos;
            lastPos = pos;
            lastState = sm->rules[pos].state;
        }
    }

    /* For each state with SMR_INHERIT, find a pointer to the super-state's first
     * rule.
     */
    rule = &sm->rules[0];
    while(rule) {

        if (rule->type == SMR_INHERIT) {
            superRule = &sm->rules[0];
            while(superRule) {
                /* Does this rule match? */
                if (superRule->state == rule->u.inherit.superState) break;

                /* Advance to next rule looking for a match */
                if (superRule->nextStatePos) {
                    superRule = &sm->rules[superRule->nextStatePos];
                } else {
                    superRule = 0;
                }
            }
            /* This assertion fails if the super-rule could not be found */
            ASSERT(superRule); 

            rule->u.inherit.superStateRule = superRule;
        }

        /* Check to see if this is the initial state */
        if (rule->state == sm->initState) {
            sm->state = rule;
        }

        /* Advance to the next rule until complete */
        if (rule->nextStatePos) {
            rule = &sm->rules[rule->nextStatePos];
        } else {
            rule = 0;
        }
    }

    /* This assertion fails if the init state could not be found */
    ASSERT(sm->state); 

    sm->flags |= SMF_FINALIZED;
}

void SM_InitFromTemplate(StateMachine *smNew, const StateMachine *smTemplate, void *context)
{
    ASSERT(smTemplate->flags & SMF_FINALIZED);

    *smNew = *smTemplate;
    smNew->context = context;
}

/* Insert an event-based transition rule */
void smOnEvent(StateMachine *sm, uint8_t state, uint32_t eventType, uint8_t nextState, SmAction action, const char *actionName)
{
    SmRule rule;

#if XA_DECODER == MICO_FALSE

    UNUSED_PARAMETER(actionName);

#endif /* XA_DECODER == MICO_FALSE */ 

    /* State machine must not have been finalized */
    ASSERT(!(sm->flags & SMF_FINALIZED));

    rule.type = SMR_EVENT;
    rule.state = state;
    rule.u.evt.eventType = eventType;
    rule.u.evt.nextState = nextState;
    rule.u.evt.action = action;
    rule.nextStatePos = 0;
#if XA_DECODER == MICO_TRUE
    rule.u.evt.actionName = actionName;
#endif /* XA_DECODER == MICO_TRUE */

    smInsertRule(sm, &rule);
}

/* Insert an exit-state action rule */
void smOnExit(StateMachine *sm, uint8_t state, SmAction action, const char *actionName)
{
    SmRule rule;

#if XA_DECODER == MICO_FALSE

    UNUSED_PARAMETER(actionName);

#endif /* XA_DECODER == MICO_FALSE */ 

    /* State machine must not have been finalized */
    ASSERT(!(sm->flags & SMF_FINALIZED));

    rule.type = SMR_EXIT;
    rule.state = state;
    rule.u.enterExit.action = action;
    rule.nextStatePos = 0;
#if XA_DECODER == MICO_TRUE
    rule.u.enterExit.actionName = actionName;
#endif /* XA_DECODER == MICO_TRUE */

    smInsertRule(sm, &rule);
}

/* Insert an enter-state action rule */
void smOnEnter(StateMachine *sm, uint8_t state, SmAction action, const char *actionName)
{
    SmRule rule;

#if XA_DECODER == MICO_FALSE

    UNUSED_PARAMETER(actionName);

#endif /* XA_DECODER == MICO_FALSE */ 

    /* State machine must not have been finalized */
    ASSERT(!(sm->flags & SMF_FINALIZED));

    rule.type = SMR_ENTER;
    rule.state = state;
    rule.u.enterExit.action = action;
    rule.nextStatePos = 0;
#if XA_DECODER == MICO_TRUE
    rule.u.enterExit.actionName = actionName;
#endif /* XA_DECODER == MICO_TRUE */

    smInsertRule(sm, &rule);
}

/* Insert an inheritance relationship */
void SM_Inherit(StateMachine *sm, uint8_t substate, uint8_t superstate)
{
    SmRule rule;

    /* State machine must not have been finalized */
    ASSERT(!(sm->flags & SMF_FINALIZED));

    rule.type = SMR_INHERIT;
    rule.state = substate;
    rule.u.inherit.superState = superstate;
    rule.u.inherit.superStateRule = 0;
    rule.nextStatePos = 0;

    smInsertRule(sm, &rule);
}

/* Insert a event-processing block */
void SM_Block(StateMachine *sm, uint8_t substate, uint32_t eventType)
{
    SmRule rule;

    /* State machine must not have been finalized */
    ASSERT(!(sm->flags & SMF_FINALIZED));

    /* A block-event is just a normal event that does nothing */
    rule.type = SMR_EVENT;
    rule.state = substate;
    rule.u.evt.nextState = substate;
    rule.u.evt.eventType = eventType;
    rule.u.evt.action = 0;
    rule.nextStatePos = 0;

    smInsertRule(sm, &rule);
}


#if XA_DECODER == MICO_TRUE
/**
 * Configures state and event decoding types
 */
void SM_EnableDecode(StateMachine *sm, mico_bool_t enable, const char *prefix)
{
    sm->decode = enable;
    sm->prefix = prefix;
}
#endif

/**
 * Handles a state, potentially causing actions and state transitions to
 * be performed.
 *
 * If the state is handled successfully, returns TRUE. If the current state does not
 * support the event, or if an action caused by this event fails, returns FALSE.
 */
mico_bool_t SM_Handle(StateMachine *sm, uint32_t eventType)
{
    const SmRule *r;
    SmRule *state, *nextState;

    ASSERT(sm);
    if (!sm->flags & SMF_FINALIZED) SM_Finalize(sm);

    state = sm->state;
    ASSERT(state);
    r = smFindEventRule(sm, state, eventType);

    /* If no rule then fail */
    if (!r) {

#if XA_DECODER == MICO_TRUE
        if (sm->decode) {
            Report("%s(%lx): %s unexpected during %s, rejecting\n", sm->prefix, (uint32_t)sm->context,
                   XAD_Decode(eventType), XAD_Decode(state->state));
        }
#endif /* XA_DECODER == MICO_TRUE */

        return FALSE;
    }

    /* Transition to next state */
    nextState = smLookupState(sm, r->u.evt.nextState);
    if (nextState == 0) {
        sm->tempState2.state = r->u.evt.nextState;
        sm->tempState2.type = SMR_NONE;
        nextState = &sm->tempState2;
    }

#if XA_DECODER == MICO_TRUE
    if (sm->decode && state != nextState) {
        Report("%s(%lx): On %s, state goes from %s to %s\n", sm->prefix, (uint32_t)sm->context,
               XAD_Decode(eventType), XAD_Decode(state->state), XAD_Decode(nextState->state));
    }
#endif /* XA_DECODER == MICO_TRUE */

    smTransition(sm, state, nextState);

    /* Pass if no action */
    if (!r->u.evt.action) return TRUE;

#if XA_DECODER == MICO_TRUE
    if (sm->decode) {
        Report("%s(%lx): On %s, calling %s()\n", sm->prefix, (uint32_t)sm->context,
               XAD_Decode(eventType), r->u.evt.actionName);
    }
#endif /* XA_DECODER == MICO_TRUE */

    /* Attempt the action */
    if (!(r->u.evt.action)(sm->context)) {

        /* Action failed; roll back the state transition */
        nextState = smLookupState(sm, r->u.evt.nextState);
        if (nextState == 0) {
            sm->tempState2.state = r->u.evt.nextState;
            sm->tempState2.type = SMR_NONE;
            nextState = &sm->tempState2;
        }

#if XA_DECODER == MICO_TRUE
        if (sm->decode) {
            Report("%s(%lx): %s() failed, rollback to %s\n", sm->prefix, (uint32_t)sm->context,
                   r->u.evt.actionName, XAD_Decode(nextState->state));
        }
#endif /* XA_DECODER == MICO_TRUE */

        smTransition(sm, nextState, state);
        return FALSE;
    }
    return TRUE;
}

/**
 * Returns the current state.
 */
uint8_t SM_GetState(StateMachine *sm)
{
    ASSERT(sm);
    if (!sm->flags & SMF_FINALIZED) SM_Finalize(sm);
    return sm->state->state;
}

/**
 * Returns the prior state. Only meaningful during action execution.
 */
uint8_t SM_GetLastState(StateMachine *sm)
{
    ASSERT(sm);
    if (!sm->flags & SMF_FINALIZED) SM_Finalize(sm);
    return sm->lastState->state;
}

/**
 * Returns true if the state machine is in the given state, or is in any substate of 
 * the given state.
 */
mico_bool_t SM_InState(StateMachine *sm, uint8_t testState)
{
    SmRule *curState;

    ASSERT(sm);
    if (!sm->flags & SMF_FINALIZED) SM_Finalize(sm);

    curState = sm->state;

    while (TRUE) {
        /* If it's a match, return */
        if (curState->state == testState) return TRUE;

        /* No parent states to check? Done */
        if (curState->type != SMR_INHERIT) return FALSE;

        /* Walk to parent and check again */
        curState = curState->u.inherit.superStateRule;
    }
    return FALSE;
}

/**
 * Transition to new state
 */
void SM_GotoState(StateMachine *sm, uint8_t newStateVal)
{
    SmRule *newState;

    ASSERT(sm);
    if (!sm->flags & SMF_FINALIZED) SM_Finalize(sm);

    ASSERT(sm->state);
    newState = smLookupState(sm, newStateVal);
    if (newState == 0) {
        sm->tempState2.state = newStateVal;
        sm->tempState2.type = SMR_NONE;
        newState = &sm->tempState2;
    }

#if XA_DECODER == MICO_TRUE
    if (sm->decode) {
        Report("%s(%lx): Manual state change from %s to %s\n", sm->prefix, (uint32_t)sm->context,
               XAD_Decode(sm->state->state), XAD_Decode(newState->state));
    }
#endif /* XA_DECODER == MICO_TRUE */

    smTransition(sm, sm->state, newState);

    /* Obliterate lastState so that if we're in an action that fails,
     * rollback does not occur.
     */
    sm->lastState = sm->state;
}

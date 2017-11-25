/****************************************************************************
 *
 * File:
 *     $Id: statemachine.h 11179 2014-07-10 00:40:10Z khe $
 *     $Product: OpenSynergy Blue SDK v4.x $
 *     $Revision: 11179 $
 *
 * Description: A generic State Machine interface
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
#ifndef __STATEMACHINE_H
#define __STATEMACHINE_H

#include "mico.h"

#define XA_DECODER MICO_TRUE

/*---------------------------------------------------------------------------
 * StateMachine API
 * 
 *     Defines a generic state machine processor. State machine behavior
 *     is specified by providing a series of rules. The rules are activated
 *     by feeding a stream of events into the state machine, automatically
 *     causing state transitions and firing user-supplied action callbacks.
 *
 *     Features of the state machine include a compact RAM footprint;
 *     support for an inheritance tree of states and superstates; actions
 *     which are triggered when the state machine transitions into or out
 *     of certain states; automatic rollback from failed actions; and
 *     human-readable debug printout of all state machine activity.
 */


/****************************************************************************
 *
 * Types
 *
 ****************************************************************************/

/* Forward structure references */
typedef struct _SmRule SmRule;

/* Internal use only */
typedef enum _SmRuleType {
    SMR_INHERIT, /* Must be first */
    SMR_ENTER,
    SMR_EXIT,
    SMR_EVENT,   /* Must be next to last */
    SMR_NONE,    /* Must be last */
} SmRuleType;

#define SMF_FINALIZED 0x01


/*---------------------------------------------------------------------------
 * SmAction callback
 *
 *     Prototype for all "action" callbacks. Actions are invoked during
 *     state transitions, or when matching events are detected.
 *
 *     SmAction callbacks will always be supplied with the context pointer
 *     provided during SM_Init(). 
 *
 *     The action returns success (TRUE or FALSE). If an action callback
 *     is invoked due to an SM_OnEvent() rule, failure of the action will
 *     cause the state machine to transition back to its original state.
 *     If the action callback is invoked due to SM_OnExit() or SM_OnEnter(),
 *     its return is ignored.
 */
typedef mico_bool_t (*SmAction)(void *context);


/*---------------------------------------------------------------------------
 * SmRule structure
 *
 *     Defines space for internal storage of rule definitions. Users must
 *     define an array of SmRule structures in RAM with sufficient space
 *     for all rules that will be defined later with SM_OnEvent, SM_OnEnter,
 *     SM_OnExit, SM_Block, and SM_Inherit.
 */
struct _SmRule {
    /* == Internal use only == */
    enum _SmRuleType type;
    uint8_t          state;
    union {

        struct {
            uint8_t    nextState;
            uint32_t   eventType;
            SmAction   action;
#if XA_DECODER == MICO_TRUE
            const char *actionName;
#endif /* XA_DECODER == MICO_TRUE */
        } evt;

        struct {
            uint32_t   eventType;
            SmAction   action;
#if XA_DECODER == MICO_TRUE
            const char *actionName;
#endif /* XA_DECODER == MICO_TRUE */
        } enterExit;

        struct {            
            uint8_t    superState;
            SmRule    *superStateRule;
        } inherit;

    } u;
    uint16_t        nextStatePos;
};

/*---------------------------------------------------------------------------
 * StateMachine structure
 * 
 *     Context memory for a state machine. This memory is provided to 
 *     SM_Init and must not be modified by the user.
 */
typedef struct _StateMachine {
    /* Internal use only */
    SmRule      *rules;
    uint16_t     ruleMax, ruleCount;
    uint8_t      initState;
    SmRule      *state, *lastState;
    uint8_t      flags;
    void        *context;
#if XA_DECODER == MICO_TRUE
    const char  *prefix;
    // const char  *stateTypeName;
    // const char  *eventTypeName;
    mico_bool_t  decode;
#endif /* XA_DECODER == MICO_TRUE */

    /* Used when there are no rules for a state. */
    SmRule  tempState, tempState2;

} StateMachine;

/* A define used to auto-expand the action function into an SmAction and text */
#if XA_DECODER == MICO_TRUE
#define ASSERT(expr)    ((void)0)
#define SM_ACTION_NAME(action) ((SmAction)(action)), #action
#define SM_NO_ACTION() 0, ""
#else /* XA_DECODER == MICO_TRUE */
#define ASSERT(expr)    ((void)0)
#define SM_ACTION_NAME(action) ((SmAction)(action)), 0
#define SM_NO_ACTION() 0, 0
#endif /* XA_DECODER == MICO_TRUE */

/* Internal prototypes */
void smOnEvent(StateMachine *sm, uint8_t state, uint32_t eventType, uint8_t nextState, SmAction action, const char *actionName);
void smOnExit(StateMachine *sm, uint8_t state, SmAction action, const char *actionName);
void smOnEnter(StateMachine *sm, uint8_t state, SmAction action, const char *actionName);


/*---------------------------------------------------------------------------
 * SmInitParms structure
 *
 *     Parameters used when initializing a StateMachine. When using this 
 *     structure, initialize it with 0's then fill in all required
 *     parameters.
 */
typedef struct _SmInitParms {

    /* Points to an uninitialized RAM buffer to be filled with rules. The
     * buffer must supply sufficient space for all rules that will later be
     * added. 
     */
    SmRule *rules;

    /* Indicates the size of the "rules" buffer. The size is expressed as
     * the maximum available number of rules which may be filled 
     * (not a sizeof or byte-count).
     */
    uint16_t maxRules;

    /* A user-defined context pointer. This pointer is provided as the
     * first argument to each action callback.
     */
    void   *context;

    /* The initial state of the state machine. Note that the machine simply
     * starts in this state; it does not "enter" it, so any OnEnter rules
     * are not triggered.
     */
    uint8_t      initState;
} SmInitParms;

/****************************************************************************
 *
 * Section: State Machine Initialization and Configuration
 *
 ****************************************************************************/

/*---------------------------------------------------------------------------
 * SM_Init()
 *
 *     Initializes a state machine for use.
 *
 *     After this call, other configuration APIs should be called (such as
 *     SM_OnEvent, etc.). Once the state machine is fully configured, the
 *     state machine is put into operation (by calling SM_Handle(), etc).
 *
 * Parameters:
 *     sm - Uninitialized memory to be used by the state machine code.
 * 
 *     parms - Setup parameters required for state machine initialization.
 */
void SM_Init(StateMachine *sm, const SmInitParms *parms);

/*---------------------------------------------------------------------------
 * SM_InitFromTemplate()
 *
 *     Initializes a state machine for use based on an existing state
 *     machine as a template.
 *
 *     After this call, the state machine is already finalized (see
 *     SM_Finalize) and ready for use.
 *
 * Parameters:
 *     sm - Uninitialized memory to be used by the state machine code.
 * 
 *     tmpl - A completely configured and finalized StateMachine to
 *         be used as a template for the new state machine's behavior.
 *         Template memory must not be freed or modified for the duration
 *         of use of the new state machine.
 *
 *     context - A context pointer which may be used to uniquely identify
 *         this state machine instance in action callbacks.
 */
void SM_InitFromTemplate(StateMachine *sm, const StateMachine *tmpl,
                         void *context);

/*---------------------------------------------------------------------------
 * SM_EnableDecode()
 *
 *     Enables/disables decoding of each action taken by the state machine.
 *
 *     Note: has no effect if XA_DECODER is disabled.
 *
 * Parameters:
 *     sm - Initialized state machine
 *
 *     enable - TRUE to enable printout, FALSE to disable
 *
 *     prefix - A string to be printed with each output message. It should
 *         supply the state machine's name.
 *
 *     stateType - The typename used by the uint8_t state type. Note that 
 *         the typename is provided without quotes when using this API. To
 *         be printed in human-readable format, XadRegister must have been 
 *         called for the typename.
 *
 *     eventType - The typename used by the uint32_t event type. Note that 
 *         the typename is provided without quotes when using this API. To
 *         be printed in human-readable format, XadRegister must have been 
 *         called for the typename.
 */
#if XA_DECODER == MICO_TRUE
void SM_EnableDecode(StateMachine *sm, mico_bool_t enable, const char *prefix);
#endif 

/*---------------------------------------------------------------------------
 * SM_OnEvent()
 *
 *     Defines behavior to take place if a certain event occurs during a
 *     certain state.
 *
 *     If the state machine is in the specified state and the event occurs
 *     (see SM_Handle()), the state machine will transition to nextState
 *     and attempt to perform the specified action. If the action fails
 *     (returns FALSE), the state machine will transition back to the
 *     original state and SM_Handle will return FALSE.
 *
 *     This API must not be used after the state machine is put into
 *     operation (SM_Handle, etc.). Also, this API must only be used
 *     once for a given state and eventType.
 *
 * Parameters:
 *     sm - An initialized state machine.
 *
 *     state - The state during which the event will be handled.
 *
 *     eventType - The event to watch for.
 *
 *     nextState - State to transition into.
 * 
 *     action - The action to perform.
 */
void SM_OnEvent(StateMachine *sm, uint8_t state, uint32_t eventType, uint8_t nextState, SmAction action);
#define SM_OnEvent(sm, state, eventType, nextState, action) \
    smOnEvent(sm, state, eventType, nextState, SM_ACTION_NAME(action))


/*---------------------------------------------------------------------------
 * SM_OnExit()
 *
 *     Defines an action to be performed when exiting a state.
 *
 *     If the new state's superstate is also being exited, the new state's 
 *     exit action is performed before the superstate's exit action.
 * 
 *     This API must not be used after the state machine is put into
 *     operation (SM_Handle, etc.). Also, this API must only be used
 *     once for a given state.
 *
 * Parameters:
 *     sm - An initialized state machine.
 *
 *     state - The state which, when exited, triggers an action.
 * 
 *     action - The action to perform. The action's return value is ignored.
 */
void SM_OnExit(StateMachine *sm, uint8_t state, SmAction action);
#define SM_OnExit(sm, state, action) \
    smOnExit(sm, state, SM_ACTION_NAME(action))

/*---------------------------------------------------------------------------
 * SM_OnEnter()
 *
 *     Defines an action to be performed when entering a state.
 *
 *     If the new state's superstate is also being entered, the superstate's
 *     enter actions are performed first.
 * 
 *     This API must not be used after the state machine is put into
 *     operation (SM_Handle, etc.). Also, this API must only be used
 *     once for a given state.
 *
 * Parameters:
 *     sm - An initialized state machine.
 *
 *     state - The state which, when entered, triggers an action.
 * 
 *     action - The action to perform. The action's return value is ignored.
 */
void SM_OnEnter(StateMachine *sm, uint8_t state, SmAction action);
#define SM_OnEnter(sm, state, action) \
        smOnEnter(sm, state, SM_ACTION_NAME(action))

/*---------------------------------------------------------------------------
 * SM_Inherit()
 *
 *     Specifies that a substate inherits all behavior of the superstate.
 *     In other words, if a substate has no behavior defined for an event
 *     or state transition, the superstate will also be searched.
 *
 *     Use of this API allows common rules to be specified once in the
 *     superstate, instead of manually re-specifying identicial behavior 
 *     in multiple states. Used properly, this can result in a more 
 *     readable and maintaintable state machine.
 *
 *     This API must not be used after the state machine is put into
 *     operation (SM_Handle, etc.). Also, this API must only be used
 *     once for a given substate and superstate.
 *
 * Parameters:
 *     sm - An initialized state machine.
 *
 *     substate - The "child" state which will inherit from "superstate".
 * 
 *     superstate - The "parent" state.
 */
void SM_Inherit(StateMachine *sm, uint8_t substate, uint8_t superstate);


/*---------------------------------------------------------------------------
 * SM_Block()
 *
 *     Blocks an event from being processed by the specified state, or
 *     any of its superstates. Blocked events will fail to result in any
 *     transition or action and will cause SM_Handle to return false.
 *
 *     This API must not be used after the state machine is put into
 *     operation (SM_Handle, etc.). Also, this API must only be used
 *     once for a given substate and eventType.
 *
 * Parameters:
 *     sm - An initialized state machine.
 *
 *     substate - The state in which the block applies.
 * 
 *     eventType - The event to be blocked.
 */
void SM_Block(StateMachine *sm, uint8_t substate, uint32_t eventType);

/*---------------------------------------------------------------------------
 * SM_Finalize()
 *
 *     Optionally "finalizes" a state machine. This API may be called when
 *     all rules have been defined. If an error in the state machine is configured,
 *     calling this API will trigger an assert.
 *
 *     
 *
 * Parameters:
 *     sm - An initialized state machine.
 *
 */
void SM_Finalize(StateMachine *sm);


/****************************************************************************
 *
 * Section: State Machine Operation
 *
 ****************************************************************************/


/*---------------------------------------------------------------------------
 * SM_Handle()
 *
 *     Supplies an event to be processed by the state machine. Depending
 *     on the event, the current state, and the rules previously set,
 *     this can trigger actions and state transitions which will be 
 *     completed before this function returns.
 *
 * Parameters:
 *     sm - An initialized state machine.
 * 
 *     eventType - Event to be processed by the state machine. If additional
 *         data must accompany the event, it should be made available via
 *         the user-supplied context pointer.
 *
 * Returns:
 *     TRUE - The event was handled successfully.
 *     FALSE - The event could not be processed because no rules were
 *         defined to handle it, or a required action failed.
 */
mico_bool_t SM_Handle(StateMachine *sm, uint32_t eventType);


/*---------------------------------------------------------------------------
 * SM_GetState()
 *
 *     Returns the current state.
 *
 * Parameters:
 *     sm - An initialized state machine.
 * 
 * Returns:
 *     The current state code.
 */
uint8_t SM_GetState(StateMachine *sm);

/*---------------------------------------------------------------------------
 * SM_InState()
 *
 *     Determines whether the state machine is in the specified state
 *     (including any of the specified state's superstates).
 *
 * Parameters:
 *     sm - An initialized state machine.
 *
 *     testState - The state to test against.
 * 
 * Returns:
 *     FALSE - The current state does not match "testState" or any of its
 *         superstates.
 *
 *     TRUE - The current state matches either "testState" or one of its
 *         superstates.
 */
mico_bool_t SM_InState(StateMachine *sm, uint8_t testState);

/*---------------------------------------------------------------------------
 * SM_GotoState()
 *
 *     Transitions directly to the specified state. This transition will
 *     trigger enter and exit rules (SM_OnEnter, SM_OnExit) as necessary.
 *
 *     Transitions should generally be caused by state machine rules. This
 *     API is supplied to handle cases where a state transition is forced
 *     by events that occur outside the state machine.
 *
 * Parameters:
 *     sm - An initialized state machine.
 *
 *     testState - The state to test against.
 * 
 */
void SM_GotoState(StateMachine *sm, uint8_t newState);

/*---------------------------------------------------------------------------
 * SM_GetLastState()
 *
 *     Returns the previous state. This API is valid only during processing
 *     of an action caused by an event-rule (SM_OnEvent).
 *
 *     Note that if SM_GotoState is called during processing of an action,
 *     the results of this API are undefined.
 *
 * Parameters:
 *     sm - An initialized state machine.
 *
 * Returns:
 *     The previous state.
 */
uint8_t SM_GetLastState(StateMachine *sm);


#endif /* __STATEMACHINE_H */



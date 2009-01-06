import os
import sys
import tempfile
from pprint import pprint

###

import Arguments
import Jobs
import Phases
import Tools
import Types
import Util

# FIXME: Clean up naming of options and arguments. Decide whether to
# rename Option and be consistent about use of Option/Arg.

####

class MissingArgumentError(ValueError):
    """MissingArgumentError - An option required an argument but none
    was given."""

###

class Driver(object):
    def __init__(self):
        self.parser = Arguments.createOptionParser()

    def run(self, argv):
        # FIXME: Things to support from environment: GCC_EXEC_PREFIX,
        # COMPILER_PATH, LIBRARY_PATH, LPATH, CC_PRINT_OPTIONS,
        # QA_OVERRIDE_GCC3_OPTIONS, ...?

        # FIXME: -V and -b processing

        # Handle some special -ccc- options used for testing which are
        # only allowed at the beginning of the command line.
        cccPrintOptions = False
        cccPrintPhases = False
        cccUseDriverDriver = True
        while argv and argv[0].startswith('-ccc-'):
            opt,argv = argv[0][5:],argv[1:]

            if opt == 'print-options':
                cccPrintOptions = True
            elif opt == 'print-phases':
                cccPrintPhases = True
            elif opt == 'no-driver-driver':
                # FIXME: Remove this once we have some way of being a
                # cross compiler driver (cross driver compiler? compiler
                # cross driver? etc.).
                cccUseDriverDriver = False
            else:
                raise ValueError,"Invalid ccc option: %r" % cccPrintOptions

        args = self.parser.parseArgs(argv)

        # FIXME: Ho hum I have just realized -Xarch_ is broken. We really
        # need to reparse the Arguments after they have been expanded by
        # -Xarch. How is this going to work?
        #
        # Scratch that, we aren't going to do that; it really disrupts the
        # organization, doesn't consistently work with gcc-dd, and is
        # confusing. Instead we are going to enforce that -Xarch_ is only
        # used with options which do not alter the driver behavior. Let's
        # hope this is ok, because the current architecture is a little
        # tied to it.

        if cccPrintOptions:
            self.printOptions(args)
            sys.exit(0)

        self.handleImmediateOptions(args)

        if cccUseDriverDriver:
            phases = self.buildPipeline(args)
        else:
            phases = self.buildNormalPipeline(args)

        if cccPrintPhases:
            self.printPhases(phases, args)
            sys.exit(0)

        if 0:
            print Util.pprint(phases)

        jobs = self.bindPhases(phases, args)

        # FIXME: We should provide some basic sanity checking of the
        # pipeline as a "verification" sort of stage. For example, the
        # pipeline should never end up writing to an output file in two
        # places (I think). The pipeline should also never end up writing
        # to an output file that is an input.
        #
        # This is intended to just be a "verify" step, not a functionality
        # step. It should catch things like the driver driver not
        # preventing -save-temps, but it shouldn't change behavior (so we
        # can turn it off in Release-Asserts builds).

        # Print in -### syntax.
        hasHashHashHash = None
        for arg in args:
            if arg.opt and arg.opt.name == '-###':
                hasHashHashHash = arg

        if hasHashHashHash:
            self.claim(hasHashHashHash)
            for j in jobs.iterjobs():
                if isinstance(j, Jobs.Command):
                    print '"%s"' % '" "'.join(j.render(argv))
                elif isinstance(j, Jobs.PipedJob):
                    for c in j.commands:
                        print '"%s" %c' % ('" "'.join(c.render(argv)),
                                           "| "[c is j.commands[-1]])
                elif not isinstance(j, JobList):
                    raise ValueError,'Encountered unknown job.'
            sys.exit(0)

        for j in jobs.iterjobs():
            if isinstance(j, Jobs.Command):
                cmd_args = j.render(argv)
                res = os.spawnvp(os.P_WAIT, cmd_args[0], cmd_args)
                if res:
                    sys.exit(res)
            elif isinstance(j, Jobs.PipedJob):
                raise NotImplementedError,"Piped jobs aren't implemented yet."
            else:
                raise ValueError,'Encountered unknown job.'

    def claim(self, option):
        # FIXME: Move to OptionList once introduced and implement.
        pass

    def warning(self, message):
        print >>sys.stderr,'%s: %s' % (sys.argv[0], message)

    def printOptions(self, args):
        for i,arg in enumerate(args):
            if isinstance(arg, Arguments.InputArg):
                name = "<input>"
            elif isinstance(arg, Arguments.UnknownArg):
                name = "<unknown>"
            else:
                assert arg.opt
                name = arg.opt.name
            if isinstance(arg, Arguments.MultipleValuesArg):
                values = list(args.getValues(arg))
            elif isinstance(arg, Arguments.ValueArg):
                values = [args.getValue(arg)]
            elif isinstance(arg, Arguments.JoinedAndSeparateValuesArg):
                values = [args.getJoinedValue(arg), args.getSeparateValue(arg)]
            else:
                values = []
            print 'Option %d - Name: "%s", Values: {%s}' % (i, name, 
                                                            ', '.join(['"%s"' % v 
                                                                       for v in values]))

    def printPhases(self, phases, args):
        def printPhase(p, f, steps, arch=None):
            if p in steps:
                return steps[p]
            elif isinstance(p, Phases.BindArchAction):
                for kid in p.inputs:
                    printPhase(kid, f, steps, p.arch)
                steps[p] = len(steps)
                return

            if isinstance(p, Phases.InputAction):
                phaseName = 'input'
                inputStr = '"%s"' % args.getValue(p.filename)
            else:
                phaseName = p.phase.name
                inputs = [printPhase(i, f, steps, arch) 
                          for i in p.inputs]
                inputStr = '{%s}' % ', '.join(map(str, inputs))
            if arch is not None:
                phaseName += '-' + args.getValue(arch)
            steps[p] = index = len(steps)
            print "%d: %s, %s, %s" % (index,phaseName,inputStr,p.type.name)
            return index
        steps = {}
        for phase in phases:
            printPhase(phase, sys.stdout, steps)

    def handleImmediateOptions(self, args):
        # FIXME: Some driver Arguments are consumed right off the bat,
        # like -dumpversion. Currently the gcc-dd handles these
        # poorly, so we should be ok handling them upfront instead of
        # after driver-driver level dispatching.
        #
        # FIXME: The actual order of these options in gcc is all over the
        # place. The -dump ones seem to be first and in specification
        # order, but there are other levels of precedence. For example,
        # -print-search-dirs is evaluated before -print-prog-name=,
        # regardless of order (and the last instance of -print-prog-name=
        # wins verse itself).
        #
        # FIXME: Do we want to report "argument unused" type errors in the
        # presence of things like -dumpmachine and -print-search-dirs?
        # Probably not.
        for arg in args:
            if arg.opt is not None:
                if arg.opt.name == '-dumpmachine':
                    print 'FIXME: %s' % arg.opt.name
                    sys.exit(1)
                elif arg.opt.name == '-dumpspecs':
                    print 'FIXME: %s' % arg.opt.name
                    sys.exit(1)
                elif arg.opt.name == '-dumpversion':
                    print 'FIXME: %s' % arg.opt.name
                    sys.exit(1)
                elif arg.opt.name == '-print-file-name=':
                    print 'FIXME: %s' % arg.opt.name
                    sys.exit(1)
                elif arg.opt.name == '-print-multi-directory':
                    print 'FIXME: %s' % arg.opt.name
                    sys.exit(1)
                elif arg.opt.name == '-print-multi-lib':
                    print 'FIXME: %s' % arg.opt.name
                    sys.exit(1)
                elif arg.opt.name == '-print-prog-name=':
                    print 'FIXME: %s' % arg.opt.name
                    sys.exit(1)
                elif arg.opt.name == '-print-libgcc-file-name':
                    print 'FIXME: %s' % arg.opt.name
                    sys.exit(1)
                elif arg.opt.name == '-print-search-dirs':
                    print 'FIXME: %s' % arg.opt.name
                    sys.exit(1)

    def buildNormalPipeline(self, args):
        hasCombine = None
        hasSyntaxOnly = None
        hasDashC = hasDashE = hasDashS = None

        inputType = None
        inputTypeOpt = None
        inputs = []
        for a in args:
            if isinstance(a, Arguments.InputArg):
                if inputType is None:
                    base,ext = os.path.splitext(args.getValue(a))
                    if ext and ext in Types.kTypeSuffixMap:
                        klass = Types.kTypeSuffixMap[ext]
                    else:
                        # FIXME: Its not clear why we shouldn't just
                        # revert to unknown. I think this is more likely a
                        # bug / unintended behavior in gcc. Not very
                        # important though.
                        klass = Types.ObjectType
                else:
                    assert inputTypeOpt is not None
                    self.claim(inputTypeOpt)
                    klass = inputType
                inputs.append((klass, a))
            elif a.opt is not None:
                # FIXME: We should warn about inconsistent and duplicate
                # usage of these flags.
                if a.opt.name == '-E':
                    hasDashE = a
                elif a.opt.name == '-S':
                    hasDashS = a
                elif a.opt.name == '-c':
                    hasDashC = a
                elif a.opt.name == '-fsyntax-only':
                    hasSyntaxOnly = a
                elif a.opt.name == '-combine':
                    hasCombine = a
                elif a.opt.name == '-filelist':
                    # FIXME: This might not be good enough. We may
                    # need to introduce another type of InputArg for
                    # this case, so that other code which needs to
                    # know the inputs handles this properly. Best not
                    # to try and lipo this, for example.
                    #
                    # Treat as a linker input.
                    inputs.append((Types.ObjectType, a))
                elif a.opt.name == '-x':
                    self.claim(a)
                    inputTypeOpt = a
                    value = args.getValue(a)
                    if value in Types.kTypeSpecifierMap:
                        inputType = Types.kTypeSpecifierMap[value]
                    else:
                        # FIXME: How are we going to handle diagnostics.
                        self.warning("language %s not recognized" % value)

                        # FIXME: Its not clear why we shouldn't just
                        # revert to unknown. I think this is more likely a
                        # bug / unintended behavior in gcc. Not very
                        # important though.
                        inputType = ObjectType

        # We claim things here so that options for which we silently allow
        # override only ever claim the used option.
        if hasCombine:
            self.claim(hasCombine)

        finalPhase = Phases.Phase.eOrderPostAssemble
        finalPhaseOpt = None

        # Determine what compilation mode we are in.
        if hasDashE:
            finalPhase = Phases.Phase.eOrderPreprocess
            finalPhaseOpt = hasDashE
        elif hasSyntaxOnly:
            finalPhase = Phases.Phase.eOrderCompile
            finalPhaseOpt = hasSyntaxOnly
        elif hasDashS:
            finalPhase = Phases.Phase.eOrderCompile
            finalPhaseOpt = hasDashS
        elif hasDashC:
            finalPhase = Phases.Phase.eOrderAssemble
            finalPhaseOpt = hasDashC    

        if finalPhaseOpt:
            self.claim(finalPhaseOpt)

        # FIXME: Support -combine.
        if hasCombine:
            raise NotImplementedError,"-combine is not yet supported."

        actions = []
        linkerInputs = []
        # FIXME: This is gross.
        linkPhase = Phases.LinkPhase()
        for klass,input in inputs:
            # Figure out what step to start at.

            # FIXME: This should be part of the input class probably?
            # Altough it doesn't quite fit there either, things like
            # asm-with-preprocess don't easily fit into a linear scheme.

            # FIXME: I think we are going to end up wanting to just build
            # a simple FSA which we run the inputs down.
            sequence = []
            if klass.preprocess:
                sequence.append(Phases.PreprocessPhase())
            if klass == Types.ObjectType:
                sequence.append(linkPhase)
            elif klass.onlyAssemble:
                sequence.extend([Phases.AssemblePhase(),
                                 linkPhase])
            elif klass.onlyPrecompile:
                sequence.append(Phases.PrecompilePhase())
            else:
                sequence.extend([Phases.CompilePhase(),
                                 Phases.AssemblePhase(),
                                 linkPhase])

            if sequence[0].order > finalPhase:
                assert finalPhaseOpt and finalPhaseOpt.opt
                # FIXME: Explain what type of input file is. Or just match
                # gcc warning.
                self.warning("%s: %s input file unused when %s is present" % (args.getValue(input),
                                                                              sequence[0].name,
                                                                              finalPhaseOpt.opt.name))
            else:
                # Build the pipeline for this file.

                current = Phases.InputAction(input, klass)
                for transition in sequence:
                    # If the current action produces no output, or we are
                    # past what the user requested, we are done.
                    if (current.type is Types.NothingType or
                        transition.order > finalPhase):
                        break
                    else:
                        if isinstance(transition, Phases.PreprocessPhase):
                            assert isinstance(klass.preprocess, Types.InputType)
                            current = Phases.JobAction(transition,
                                                       [current],
                                                       klass.preprocess)
                        elif isinstance(transition, Phases.PrecompilePhase):
                            current = Phases.JobAction(transition,
                                                       [current],
                                                       Types.PCHType)
                        elif isinstance(transition, Phases.CompilePhase):
                            if hasSyntaxOnly:
                                output = Types.NothingType
                            else:
                                output = Types.AsmTypeNoPP
                            current = Phases.JobAction(transition,
                                                       [current],
                                                       output)
                        elif isinstance(transition, Phases.AssemblePhase):
                            current = Phases.JobAction(transition,
                                                       [current],
                                                       Types.ObjectType)
                        elif transition is linkPhase:
                            linkerInputs.append(current)
                            current = None
                            break
                        else:
                            raise RuntimeError,'Unrecognized transition: %s.' % transition
                        pass

                if current is not None:
                    assert not isinstance(current, Phases.InputAction)
                    actions.append(current)

        if linkerInputs:
            actions.append(Phases.JobAction(linkPhase,
                                            linkerInputs, 
                                            Types.ImageType))

        return actions

    def buildPipeline(self, args):
        # FIXME: We need to handle canonicalization of the specified arch.

        archs = []
        hasOutput = None
        hasDashM = hasSaveTemps = None
        for arg in args:
            if arg.opt is None:
                continue

            if isinstance(arg, Arguments.ValueArg):
                if arg.opt.name == '-arch':
                    archs.append(arg)
            elif arg.opt.name.startswith('-M'):
                hasDashM = arg
            elif arg.opt.name in ('-save-temps','--save-temps'):
                hasSaveTemps = arg

        if not archs:
            # FIXME: Need to infer arch so that we sub -Xarch
            # correctly.
            archs.append(Arguments.DerivedArg('i386'))

        actions = self.buildNormalPipeline(args)

        # FIXME: Use custom exception for this.
        #
        # FIXME: We killed off some others but these aren't yet detected in
        # a functional manner. If we added information to jobs about which
        # "auxiliary" files they wrote then we could detect the conflict
        # these cause downstream.
        if len(archs) > 1:
            if hasDashM:
                raise ValueError,"Cannot use -M options with multiple arch flags."
            elif hasSaveTemps:
                raise ValueError,"Cannot use -save-temps with multiple arch flags."

        # Execute once per arch.
        finalActions = []
        for p in actions:
            #  Make sure we can lipo this kind of output. If not (and it
            #  is an actual output) then we disallow, since we can't
            #  create an output file with the right name without
            #  overwriting it. We could remove this oddity by just
            #  changing the output names to include the arch, which would
            #  also fix -save-temps. Compatibility wins for now.
            #
            # FIXME: Is this error substantially less useful than
            # gcc-dd's? The main problem is that "Cannot use compiler
            # output with multiple arch flags" won't make sense to most
            # developers.
            if (len(archs) > 1 and
                p.type not in (Types.NothingType,Types.ObjectType,Types.ImageType)):
                raise ValueError,'Cannot use %s output with multiple arch flags.' % p.type.name

            inputs = []
            for arch in archs:
                inputs.append(Phases.BindArchAction(p, arch))

            # Lipo if necessary. We do it this way because we need to set
            # the arch flag so that -Xarch_ gets rewritten.
            if len(inputs) == 1 or p.type == Types.NothingType:
                finalActions.extend(inputs)
            else:
                finalActions.append(Phases.JobAction(Phases.LipoPhase(),
                                                     inputs, 
                                                     p.type))

        # FIXME: We need to add -Wl,arch_multiple and -Wl,final_output in
        # certain cases. This may be icky because we need to figure out the
        # mode first. Current plan is to hack on the pipeline once it is built
        # and we know what is being spit out. This avoids having to handling
        # things like -c and -combine in multiple places.
        #
        # The annoying one of these is -Wl,final_output because it involves
        # communication across different phases.
        #
        # Hopefully we can do this purely as part of the binding, but
        # leaving comment here for now until it is clear this works.

        return finalActions

    def bindPhases(self, phases, args):
        jobs = Jobs.JobList()

        finalOutput = None
        hasSaveTemps = hasNoIntegratedCPP = hasPipe = None
        forward = []
        for a in args:
            if isinstance(a, Arguments.InputArg):
                pass
            elif a.opt is not None:
                if a.opt.name == '-save-temps':
                    hasSaveTemps = a
                elif a.opt.name == '-no-integrated-cpp':
                    hasNoIntegratedCPP = a
                elif a.opt.name == '-o':
                    finalOutput = a
                elif a.opt.name == '-pipe':
                    hasPipe = a
                elif a.opt.name in ('-E', '-S', '-c',
                                    '-arch', '-fsyntax-only', '-combine', '-x',
                                    '-###'):
                    pass
                else:
                    forward.append(a)
            else:
                forward.append(a)

        # We claim things here so that options for which we silently allow
        # override only ever claim the used option.
        if hasPipe:
            self.claim(hasPipe)
            # FIXME: Hack, override -pipe till we support it.
            hasPipe = None
        # Claim these here. Its not completely accurate but any warnings
        # about these being unused are likely to be noise anyway.
        if hasSaveTemps:
            self.claim(hasSaveTemps)
        if hasNoIntegratedCPP:
            self.claim(hasNoIntegratedCPP)

        toolMap = {
            Phases.PreprocessPhase : Tools.GCC_PreprocessTool(),
            Phases.CompilePhase : Tools.GCC_CompileTool(),
            Phases.PrecompilePhase : Tools.GCC_PrecompileTool(),
            Phases.AssemblePhase : Tools.DarwinAssemblerTool(),
            Phases.LinkPhase : Tools.Collect2Tool(),
            Phases.LipoPhase : Tools.LipoTool(),
            }

        class InputInfo:
            def __init__(self, source, type, baseInput):
                self.source = source
                self.type = type
                self.baseInput = baseInput

            def __repr__(self):
                return '%s(%r, %r, %r)' % (self.__class__.__name__,
                                           self.source, self.type, self.baseInput)

        def createJobs(phase, forwardArgs,
                       canAcceptPipe=False, atTopLevel=False, arch=None):
            if isinstance(phase, Phases.InputAction):
                return InputInfo(phase.filename, phase.type, phase.filename)
            elif isinstance(phase, Phases.BindArchAction):
                archName = args.getValue(phase.arch)
                filteredArgs = []
                for arg in forwardArgs:
                    if arg.opt is None:
                        filteredArgs.append(arg)
                    elif arg.opt.name == '-arch':
                        if arg is phase.arch:
                            filteredArgs.append(arg)
                    elif arg.opt.name == '-Xarch_':
                        # FIXME: gcc-dd has another conditional for passing
                        # through, when the arch conditional array has an empty
                        # string. Why?
                        if args.getJoinedValue(arg) == archName:
                            # FIXME: This is wrong, we don't want a
                            # DerivedArg we want an actual parsed version
                            # of this arg.
                            filteredArgs.append(Arguments.DerivedArg(args.getSeparateValue(arg)))
                    else:
                        filteredArgs.append(arg)
                        
                return createJobs(phase.inputs[0], filteredArgs,
                                  canAcceptPipe, atTopLevel, phase.arch)

            assert isinstance(phase, Phases.JobAction)
            tool = toolMap[phase.phase.__class__]

            # See if we should use an integrated CPP. We only use an
            # integrated cpp when we have exactly one input, since this is
            # the only use case we care about.
            useIntegratedCPP = False
            inputList = phase.inputs
            if (not hasNoIntegratedCPP and 
                not hasSaveTemps and
                tool.hasIntegratedCPP()):
                if (len(phase.inputs) == 1 and 
                    isinstance(phase.inputs[0].phase, Phases.PreprocessPhase)):
                    useIntegratedCPP = True
                    inputList = phase.inputs[0].inputs

            # Only try to use pipes when exactly one input.
            canAcceptPipe = len(inputList) == 1 and tool.acceptsPipedInput()
            inputs = [createJobs(p, forwardArgs, canAcceptPipe, False, arch) for p in inputList]

            # Determine if we should output to a pipe.
            canOutputToPipe = canAcceptPipe and tool.canPipeOutput()
            outputToPipe = False
            if canOutputToPipe:
                # Some things default to writing to a pipe if the final
                # phase and there was no user override.  
                # 
                # FIXME: What is the best way to handle this?
                if (atTopLevel and 
                    isinstance(phase, Phases.PreprocessPhase) and 
                    not finalOutput):
                    outputToPipe = True
                elif hasPipe:
                    outputToPipe = True

            # Figure out where to put the job (pipes).
            jobList = jobs
            if canAcceptPipe and isinstance(inputs[0].source, Jobs.PipedJob):
                jobList = inputs[0].source

            # Figure out where to put the output.
            baseInput = inputs[0].baseInput
            if phase.type == Types.NothingType:
                output = None            
            elif outputToPipe:
                if isinstance(jobList, Jobs.PipedJob):
                    output = jobList
                else:
                    jobList = output = Jobs.PipedJob([])
                    jobs.addJob(output)
            else:
                # Figure out what the derived output location would be.
                # 
                # FIXME: gcc has some special case in here so that it doesn't
                # create output files if they would conflict with an input.
                inputName = args.getValue(baseInput)
                if phase.type is Types.ImageType:
                    namedOutput = "a.out"
                else:
                    base,_ = os.path.splitext(inputName)
                    assert phase.type.tempSuffix is not None
                    namedOutput = base + phase.type.tempSuffix

                # Output to user requested destination?
                if atTopLevel and finalOutput:
                    output = finalOutput
                # Contruct a named destination?
                elif atTopLevel or hasSaveTemps:
                    output = Arguments.DerivedArg(namedOutput)
                else:
                    # Output to temp file...
                    fd,filename = tempfile.mkstemp(suffix=phase.type.tempSuffix)
                    output = Arguments.DerivedArg(filename)

            tool.constructJob(phase, arch, jobList, inputs, output, phase.type, forwardArgs)

            return InputInfo(output, phase.type, baseInput)

        # It is an error to provide a -o option if we are making multiple
        # output files.
        if finalOutput and len([a for a in phases if a.type is not Types.NothingType]) > 1:
            # FIXME: Custom exception.
            raise ValueError,"Cannot specify -o when generating multiple files."

        for phase in phases:
            createJobs(phase, forward, canAcceptPipe=True, atTopLevel=True)

        return jobs

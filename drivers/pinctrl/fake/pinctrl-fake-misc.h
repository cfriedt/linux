#ifndef PINCTRL_FAKE_MISC_H_
#define PINCTRL_FAKE_MISC_H_

#ifndef MODULE_DESC
#define MODULE_DESC "Fake Pinctrl Driver"
#endif

#ifndef _pr_info
#define _pr_info( fmt, args... ) pr_info( MODULE_DESC ": " fmt, ##args )
#endif

#ifndef _pr_err
#define _pr_err( fmt, args... ) pr_err( MODULE_DESC ": " fmt, ##args )
#endif

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif // EXIT_SUCCESS

#endif /* PINCTRL_FAKE_MISC_H_ */
